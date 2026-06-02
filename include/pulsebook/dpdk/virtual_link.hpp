#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rte_eth_ring.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_pause.h>
#include <rte_ring.h>

#include "pulsebook/dpdk/dpdk_config.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace pulsebook::dpdk {

struct VirtualLinkStatistics {
    std::uint64_t generator_tx{};
    std::uint64_t generator_rx{};
    std::uint64_t engine_tx{};
    std::uint64_t engine_rx{};
};

class VirtualLink {
public:
    VirtualLink() = default;

    VirtualLink(const VirtualLink&) = delete;
    VirtualLink& operator=(const VirtualLink&) = delete;

    ~VirtualLink() {
        shutdown();
    }

    [[nodiscard]] bool initialize() noexcept {
        pool_ = rte_pktmbuf_pool_create(
            kMempoolName,
            kMempoolSize,
            kMempoolCacheSize,
            0,
            kDataRoomSize,
            rte_socket_id());

        if (pool_ == nullptr) {
            return false;
        }

        market_data_ring_ = rte_ring_create(
            kMarketDataRingName,
            kRingSize,
            static_cast<int>(rte_socket_id()),
            RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (market_data_ring_ == nullptr) {
            return false;
        }

        outbound_order_ring_ = rte_ring_create(
            kOutboundOrderRingName,
            kRingSize,
            static_cast<int>(rte_socket_id()),
            RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (outbound_order_ring_ == nullptr) {
            return false;
        }

        rte_ring* generator_rx[] = {outbound_order_ring_};
        rte_ring* generator_tx[] = {market_data_ring_};
        rte_ring* engine_rx[] = {market_data_ring_};
        rte_ring* engine_tx[] = {outbound_order_ring_};

        const int generator_result = rte_eth_from_rings(
            kGeneratorPortName,
            generator_rx,
            1,
            generator_tx,
            1,
            rte_socket_id());

        if (generator_result < 0) {
            return false;
        }

        const int engine_result = rte_eth_from_rings(
            kEnginePortName,
            engine_rx,
            1,
            engine_tx,
            1,
            rte_socket_id());

        if (engine_result < 0) {
            return false;
        }

        generator_port_ = static_cast<std::uint16_t>(generator_result);
        engine_port_ = static_cast<std::uint16_t>(engine_result);

        if (!configure_port(generator_port_) ||
            !configure_port(engine_port_)) {
            return false;
        }

        running_ = true;
        return true;
    }

    [[nodiscard]] bool send_generator_to_engine(
        const wire::EthernetFrame& frame) noexcept {
        return transmit(generator_port_, frame);
    }

    [[nodiscard]] bool receive_at_engine(
        wire::EthernetFrame& frame) noexcept {
        return receive(engine_port_, frame);
    }

    [[nodiscard]] bool send_engine_to_generator(
        const wire::EthernetFrame& frame) noexcept {
        return transmit(engine_port_, frame);
    }

    [[nodiscard]] bool receive_at_generator(
        wire::EthernetFrame& frame) noexcept {
        return receive(generator_port_, frame);
    }

    [[nodiscard]] std::uint16_t generator_port() const noexcept {
        return generator_port_;
    }

    [[nodiscard]] std::uint16_t engine_port() const noexcept {
        return engine_port_;
    }

    [[nodiscard]] VirtualLinkStatistics statistics() const noexcept {
        rte_eth_stats generator_stats{};
        rte_eth_stats engine_stats{};

        rte_eth_stats_get(generator_port_, &generator_stats);
        rte_eth_stats_get(engine_port_, &engine_stats);

        return {
            generator_stats.opackets,
            generator_stats.ipackets,
            engine_stats.opackets,
            engine_stats.ipackets,
        };
    }

    void shutdown() noexcept {
        if (running_) {
            rte_eth_dev_stop(engine_port_);
            rte_eth_dev_close(engine_port_);

            rte_eth_dev_stop(generator_port_);
            rte_eth_dev_close(generator_port_);

            running_ = false;
        }

        if (outbound_order_ring_ != nullptr) {
            rte_ring_free(outbound_order_ring_);
            outbound_order_ring_ = nullptr;
        }

        if (market_data_ring_ != nullptr) {
            rte_ring_free(market_data_ring_);
            market_data_ring_ = nullptr;
        }

        if (pool_ != nullptr) {
            rte_mempool_free(pool_);
            pool_ = nullptr;
        }
    }

private:
    [[nodiscard]] bool configure_port(
        const std::uint16_t port_id) noexcept {
        rte_eth_conf configuration{};

        if (rte_eth_dev_configure(
                port_id,
                kRxQueues,
                kTxQueues,
                &configuration) < 0) {
            return false;
        }

        if (rte_eth_rx_queue_setup(
                port_id,
                0,
                kRxDescriptors,
                rte_socket_id(),
                nullptr,
                pool_) < 0) {
            return false;
        }

        if (rte_eth_tx_queue_setup(
                port_id,
                0,
                kTxDescriptors,
                rte_socket_id(),
                nullptr) < 0) {
            return false;
        }

        return rte_eth_dev_start(port_id) >= 0;
    }

    [[nodiscard]] bool transmit(
        const std::uint16_t port_id,
        const wire::EthernetFrame& frame) noexcept {
        rte_mbuf* packet = rte_pktmbuf_alloc(pool_);

        if (packet == nullptr) {
            return false;
        }

        void* destination = rte_pktmbuf_append(
            packet,
            static_cast<std::uint16_t>(frame.size()));

        if (destination == nullptr) {
            rte_pktmbuf_free(packet);
            return false;
        }

        std::memcpy(destination, frame.data(), frame.size());

        rte_mbuf* packets[] = {packet};

        if (rte_eth_tx_burst(port_id, 0, packets, 1) != 1) {
            rte_pktmbuf_free(packet);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool receive(
        const std::uint16_t port_id,
        wire::EthernetFrame& frame) noexcept {
        std::array<rte_mbuf*, kBurstSize> packets{};

        for (std::uint32_t attempt = 0;
             attempt < kReceivePollAttempts;
             ++attempt) {
            const std::uint16_t received = rte_eth_rx_burst(
                port_id,
                0,
                packets.data(),
                kBurstSize);

            if (received == 0) {
                rte_pause();
                continue;
            }

            const rte_mbuf* first_packet = packets[0];

            const bool valid_length =
                rte_pktmbuf_data_len(first_packet) >= frame.size();

            if (valid_length) {
                const auto* data =
                    rte_pktmbuf_mtod(first_packet, const std::byte*);

                std::memcpy(frame.data(), data, frame.size());
            }

            for (std::uint16_t index = 0;
                 index < received;
                 ++index) {
                rte_pktmbuf_free(packets[index]);
            }

            return valid_length;
        }

        return false;
    }

    rte_mempool* pool_{};
    rte_ring* market_data_ring_{};
    rte_ring* outbound_order_ring_{};
    std::uint16_t generator_port_{};
    std::uint16_t engine_port_{};
    bool running_{};
};

}  // namespace pulsebook::dpdk
