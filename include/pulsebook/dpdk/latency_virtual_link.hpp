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

inline constexpr const char* kLatencyMempoolName = "pb_lmp";
inline constexpr const char* kLatencyMarketRingName = "pb_lmd";
inline constexpr const char* kLatencyOrderRingName = "pb_lor";
inline constexpr const char* kLatencyGeneratorPortName = "pb_lgen";
inline constexpr const char* kLatencyEnginePortName = "pb_leng";

struct LatencyLinkStatistics {
    std::uint64_t generator_tx{};
    std::uint64_t generator_rx{};
    std::uint64_t engine_tx{};
    std::uint64_t engine_rx{};
};

class LatencyVirtualLink {
public:
    LatencyVirtualLink() = default;

    LatencyVirtualLink(const LatencyVirtualLink&) = delete;
    LatencyVirtualLink& operator=(const LatencyVirtualLink&) = delete;

    ~LatencyVirtualLink() {
        shutdown();
    }

    [[nodiscard]] bool initialize() noexcept {
        pool_ = rte_pktmbuf_pool_create(
            kLatencyMempoolName,
            kMempoolSize,
            kMempoolCacheSize,
            0,
            kDataRoomSize,
            rte_socket_id());

        if (pool_ == nullptr) {
            return false;
        }

        market_ring_ = rte_ring_create(
            kLatencyMarketRingName,
            kRingSize,
            static_cast<int>(rte_socket_id()),
            RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (market_ring_ == nullptr) {
            return false;
        }

        order_ring_ = rte_ring_create(
            kLatencyOrderRingName,
            kRingSize,
            static_cast<int>(rte_socket_id()),
            RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (order_ring_ == nullptr) {
            return false;
        }

        rte_ring* generator_rx[] = {order_ring_};
        rte_ring* generator_tx[] = {market_ring_};

        rte_ring* engine_rx[] = {market_ring_};
        rte_ring* engine_tx[] = {order_ring_};

        const int generator_result = rte_eth_from_rings(
            kLatencyGeneratorPortName,
            generator_rx,
            1,
            generator_tx,
            1,
            rte_socket_id());

        if (generator_result < 0) {
            return false;
        }

        const int engine_result = rte_eth_from_rings(
            kLatencyEnginePortName,
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
        ports_created_ = true;

        if (!configure_port(generator_port_) ||
            !configure_port(engine_port_)) {
            return false;
        }

        running_ = true;
        return true;
    }

    [[nodiscard]] rte_mempool* mempool() noexcept {
        return pool_;
    }

    [[nodiscard]] std::uint16_t generator_port() const noexcept {
        return generator_port_;
    }

    [[nodiscard]] std::uint16_t engine_port() const noexcept {
        return engine_port_;
    }

    [[nodiscard]] bool inject_market_frame(
        const wire::EthernetFrame& frame) noexcept {
        rte_mbuf* packet = frame_to_mbuf(frame);

        if (packet == nullptr) {
            return false;
        }

        rte_mbuf* packets[] = {packet};

        if (rte_eth_tx_burst(generator_port_, 0, packets, 1) != 1) {
            rte_pktmbuf_free(packet);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool receive_engine_packet(
        rte_mbuf*& packet) noexcept {
        for (std::uint32_t attempt = 0;
             attempt < kReceivePollAttempts;
             ++attempt) {
            rte_mbuf* packets[1]{};

            if (rte_eth_rx_burst(engine_port_, 0, packets, 1) == 1) {
                packet = packets[0];
                return true;
            }

            rte_pause();
        }

        packet = nullptr;
        return false;
    }

    [[nodiscard]] bool transmit_engine_packet(
        rte_mbuf* packet) noexcept {
        rte_mbuf* packets[] = {packet};

        if (rte_eth_tx_burst(engine_port_, 0, packets, 1) != 1) {
            rte_pktmbuf_free(packet);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool drain_generated_order(
        wire::EthernetFrame& output) noexcept {
        for (std::uint32_t attempt = 0;
             attempt < kReceivePollAttempts;
             ++attempt) {
            rte_mbuf* packets[1]{};

            if (rte_eth_rx_burst(generator_port_, 0, packets, 1) != 1) {
                rte_pause();
                continue;
            }

            rte_mbuf* packet = packets[0];

            const bool valid_size =
                rte_pktmbuf_data_len(packet) >= output.size();

            if (valid_size) {
                const auto* data =
                    rte_pktmbuf_mtod(packet, const std::byte*);

                std::memcpy(
                    output.data(),
                    data,
                    output.size());
            }

            rte_pktmbuf_free(packet);
            return valid_size;
        }

        return false;
    }

    [[nodiscard]] LatencyLinkStatistics statistics() const noexcept {
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
        if (ports_created_) {
            if (running_) {
                rte_eth_dev_stop(engine_port_);
                rte_eth_dev_stop(generator_port_);
                running_ = false;
            }

            rte_eth_dev_close(engine_port_);
            rte_eth_dev_close(generator_port_);
            ports_created_ = false;
        }

        if (order_ring_ != nullptr) {
            rte_ring_free(order_ring_);
            order_ring_ = nullptr;
        }

        if (market_ring_ != nullptr) {
            rte_ring_free(market_ring_);
            market_ring_ = nullptr;
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

    [[nodiscard]] rte_mbuf* frame_to_mbuf(
        const wire::EthernetFrame& frame) noexcept {
        rte_mbuf* packet = rte_pktmbuf_alloc(pool_);

        if (packet == nullptr) {
            return nullptr;
        }

        void* destination = rte_pktmbuf_append(
            packet,
            static_cast<std::uint16_t>(frame.size()));

        if (destination == nullptr) {
            rte_pktmbuf_free(packet);
            return nullptr;
        }

        std::memcpy(destination, frame.data(), frame.size());
        return packet;
    }

    rte_mempool* pool_{};
    rte_ring* market_ring_{};
    rte_ring* order_ring_{};
    std::uint16_t generator_port_{};
    std::uint16_t engine_port_{};
    bool ports_created_{};
    bool running_{};
};

}  // namespace pulsebook::dpdk
