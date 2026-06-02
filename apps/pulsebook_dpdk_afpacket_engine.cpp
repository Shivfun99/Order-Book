#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_pause.h>
#include <rte_version.h>

#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

inline constexpr std::uint16_t kRxQueues = 1;
inline constexpr std::uint16_t kTxQueues = 1;
inline constexpr std::uint16_t kRxDescriptors = 512;
inline constexpr std::uint16_t kTxDescriptors = 512;
inline constexpr std::uint32_t kMempoolSize = 4095;
inline constexpr std::uint16_t kDataRoomSize = 2048;
inline constexpr std::uint16_t kBurstSize = 8;
inline constexpr std::uint64_t kTimeoutSeconds = 30;
inline constexpr const char* kMempoolName = "pb_amp";

struct Counters {
    std::uint64_t rx_packets{};
    std::uint64_t valid_market_packets{};
    std::uint64_t invalid_packets{};
    std::uint64_t no_signal_events{};
    std::uint64_t orders_emitted{};
    std::uint64_t risk_rejected{};
    std::uint64_t outbox_full{};
};

bool configure_port(const std::uint16_t port_id,
                    rte_mempool* const mempool) noexcept {
    rte_eth_conf configuration{};

    int result = rte_eth_dev_configure(
        port_id,
        kRxQueues,
        kTxQueues,
        &configuration);

    if (result < 0) {
        std::fprintf(stderr,
                     "rte_eth_dev_configure failed: %s\n",
                     rte_strerror(-result));
        return false;
    }

    std::uint16_t rx_descriptors = kRxDescriptors;
    std::uint16_t tx_descriptors = kTxDescriptors;

    result = rte_eth_dev_adjust_nb_rx_tx_desc(
        port_id,
        &rx_descriptors,
        &tx_descriptors);

    if (result < 0) {
        std::fprintf(stderr,
                     "rte_eth_dev_adjust_nb_rx_tx_desc failed: %s\n",
                     rte_strerror(-result));
        return false;
    }

    result = rte_eth_rx_queue_setup(
        port_id,
        0,
        rx_descriptors,
        rte_socket_id(),
        nullptr,
        mempool);

    if (result < 0) {
        std::fprintf(stderr,
                     "rte_eth_rx_queue_setup failed: %s\n",
                     rte_strerror(-result));
        return false;
    }

    result = rte_eth_tx_queue_setup(
        port_id,
        0,
        tx_descriptors,
        rte_socket_id(),
        nullptr);

    if (result < 0) {
        std::fprintf(stderr,
                     "rte_eth_tx_queue_setup failed: %s\n",
                     rte_strerror(-result));
        return false;
    }

    result = rte_eth_dev_start(port_id);

    if (result < 0) {
        std::fprintf(stderr,
                     "rte_eth_dev_start failed: %s\n",
                     rte_strerror(-result));
        return false;
    }

    return true;
}

pulsebook::EngineConfig make_engine_config() noexcept {
    pulsebook::EngineConfig config{};

    config.instrument_id = 77;
    config.strategy.imbalance_threshold_bps = 6000;
    config.strategy.order_quantity = 10;

    config.risk.max_order_quantity = 100;
    config.risk.max_absolute_position = 1000;
    config.risk.max_notional_ticks = 1'000'000'000ULL;
    config.risk.max_outstanding_orders = 64;

    return config;
}

pulsebook::wire::EthernetEnvelope make_response_envelope(
    const pulsebook::wire::EthernetEnvelope& request) noexcept {
    pulsebook::wire::EthernetEnvelope response{};
    response.destination = request.source;
    response.source = request.destination;
    return response;
}

bool transmit_order(
    const std::uint16_t port_id,
    rte_mempool* const mempool,
    const pulsebook::wire::EthernetEnvelope& envelope,
    const pulsebook::OrderRequest& order,
    std::uint32_t& outbound_sequence) noexcept {
    pulsebook::wire::OutboundOrderMessage wire_order{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_wire_order(
            order,
            outbound_sequence++,
            wire_order)) {
        return false;
    }

    pulsebook::wire::EthernetFrame frame{};

    if (!pulsebook::wire::encode_order_frame(
            envelope,
            wire_order,
            frame)) {
        return false;
    }

    rte_mbuf* const packet = rte_pktmbuf_alloc(mempool);

    if (packet == nullptr) {
        return false;
    }

    void* const destination = rte_pktmbuf_append(
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

}  // namespace

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        std::fprintf(stderr, "PulseBook AF_PACKET failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    const std::uint16_t port_count = rte_eth_dev_count_avail();

    if (port_count == 0) {
        std::fprintf(stderr,
                     "PulseBook AF_PACKET failure: no DPDK port found. "
                     "Start with --vdev=eth_af_packet0,iface=pb_eng,qpairs=1\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    std::uint16_t port_id = 0;
    bool found_port = false;
    std::uint16_t candidate_port{};

    RTE_ETH_FOREACH_DEV(candidate_port) {
        port_id = candidate_port;
        found_port = true;
        break;
    }

    if (!found_port) {
        std::fprintf(stderr, "PulseBook AF_PACKET failure: port enumeration failed\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_mempool* const mempool = rte_pktmbuf_pool_create(
        kMempoolName,
        kMempoolSize,
        0,
        0,
        kDataRoomSize,
        rte_socket_id());

    if (mempool == nullptr) {
        std::fprintf(stderr,
                     "PulseBook AF_PACKET failure: mempool creation failed: %s\n",
                     rte_strerror(rte_errno));
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    if (!configure_port(port_id, mempool)) {
        rte_mempool_free(mempool);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    pulsebook::TradingEngine<64> engine(make_engine_config());
    Counters counters{};
    std::uint32_t outbound_sequence = 5001;

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 9B - DPDK AF_PACKET Engine\n");
    std::printf("============================================================\n");
    std::printf("DPDK version:              %s\n", rte_version());
    std::printf("Main lcore:                %u\n", rte_lcore_id());
    std::printf("Available DPDK ports:      %u\n", port_count);
    std::printf("Selected port:             %u\n", port_id);
    std::printf("Expected Linux interface:  pb_eng\n");
    std::printf("Waiting for market-data packets...\n");
    std::fflush(stdout);

    const std::uint64_t deadline =
        rte_get_timer_cycles() +
        (rte_get_timer_hz() * kTimeoutSeconds);

    bool success = true;

    while (rte_get_timer_cycles() < deadline &&
           counters.orders_emitted == 0) {
        rte_mbuf* packets[kBurstSize]{};

        const std::uint16_t received = rte_eth_rx_burst(
            port_id,
            0,
            packets,
            kBurstSize);

        if (received == 0) {
            rte_pause();
            continue;
        }

        for (std::uint16_t index = 0; index < received; ++index) {
            rte_mbuf* const packet = packets[index];
            ++counters.rx_packets;

            const auto* const data =
                rte_pktmbuf_mtod(packet, const std::byte*);

            const auto bytes = std::span<const std::byte>(
                data,
                static_cast<std::size_t>(rte_pktmbuf_data_len(packet)));

            pulsebook::wire::EthernetEnvelope request_envelope{};
            pulsebook::wire::MarketDataMessage wire_message{};

            const auto decode_result =
                pulsebook::wire::decode_market_data_frame(
                    bytes,
                    request_envelope,
                    wire_message);

            rte_pktmbuf_free(packet);

            if (decode_result != pulsebook::wire::FrameDecodeError::none) {
                ++counters.invalid_packets;
                continue;
            }

            ++counters.valid_market_packets;

            pulsebook::MarketUpdate update{};

            if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
                    wire_message,
                    update)) {
                ++counters.invalid_packets;
                continue;
            }

            const pulsebook::EngineResult result =
                engine.on_market_update(update);

            switch (result.status) {
                case pulsebook::EngineStatus::no_order:
                    ++counters.no_signal_events;
                    break;

                case pulsebook::EngineStatus::risk_rejected:
                    ++counters.risk_rejected;
                    break;

                case pulsebook::EngineStatus::outbox_full:
                    ++counters.outbox_full;
                    break;

                case pulsebook::EngineStatus::invalid_update:
                    ++counters.invalid_packets;
                    break;

                case pulsebook::EngineStatus::order_emitted: {
                    pulsebook::OrderRequest order{};

                    if (!engine.pop_order(order) ||
                        !transmit_order(
                            port_id,
                            mempool,
                            make_response_envelope(request_envelope),
                            order,
                            outbound_sequence)) {
                        success = false;
                        break;
                    }

                    ++counters.orders_emitted;
                    break;
                }
            }

            if (!success || counters.orders_emitted > 0) {
                break;
            }
        }
    }

    if (counters.orders_emitted != 1) {
        std::fprintf(stderr,
                     "PulseBook AF_PACKET failure: no generated order was transmitted\n");
        success = false;
    }

    rte_eth_stats statistics{};
    rte_eth_stats_get(port_id, &statistics);

    if (success) {
        std::printf("\nAF_PACKET engine result:\n");
        std::printf("  RX packets observed:      %llu\n",
                    static_cast<unsigned long long>(counters.rx_packets));
        std::printf("  Valid market packets:     %llu\n",
                    static_cast<unsigned long long>(counters.valid_market_packets));
        std::printf("  No-signal events:         %llu\n",
                    static_cast<unsigned long long>(counters.no_signal_events));
        std::printf("  Orders emitted:           %llu\n",
                    static_cast<unsigned long long>(counters.orders_emitted));
        std::printf("  Invalid packets:          %llu\n",
                    static_cast<unsigned long long>(counters.invalid_packets));
        std::printf("  Risk rejected:            %llu\n",
                    static_cast<unsigned long long>(counters.risk_rejected));
        std::printf("  PMD RX packets:           %llu\n",
                    static_cast<unsigned long long>(statistics.ipackets));
        std::printf("  PMD TX packets:           %llu\n",
                    static_cast<unsigned long long>(statistics.opackets));
        std::printf("\nClassification:\n");
        std::printf("  Kernel-backed DPDK AF_PACKET functional path.\n");
        std::printf("  This is not VFIO hardware-bypass or physical NIC latency.\n");
        std::printf("\nStatus:                    OK\n");
    }

    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);
    rte_mempool_free(mempool);

    const int cleanup_result = rte_eal_cleanup();

    if (!success || cleanup_result != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
