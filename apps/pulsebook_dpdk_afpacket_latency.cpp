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
#include <rte_mempool.h>
#include <rte_pause.h>
#include <rte_version.h>

#include "pulsebook/dpdk/dpdk_latency_recorder.hpp"
#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

inline constexpr std::uint16_t kRxQueues = 1;
inline constexpr std::uint16_t kTxQueues = 1;
inline constexpr std::uint16_t kRxDescriptors = 512;
inline constexpr std::uint16_t kTxDescriptors = 512;
inline constexpr std::uint16_t kBurstSize = 8;

inline constexpr std::uint32_t kMempoolSize = 8191;
inline constexpr std::uint16_t kDataRoomSize = 4096;
inline constexpr const char* kMempoolName = "pb_almp";

inline constexpr std::size_t kSeedPackets = 10;
inline constexpr std::size_t kWarmupOrders = 100'000;
inline constexpr std::size_t kMeasuredOrders = 1'000'000;
inline constexpr std::uint64_t kTimeoutSeconds = 600;

struct ProcessingCounters {
    std::uint64_t received_packets{};
    std::uint64_t valid_market_packets{};
    std::uint64_t invalid_packets{};
    std::uint64_t seed_no_signal_events{};
    std::uint64_t warmup_orders{};
    std::uint64_t measured_orders{};
    std::uint64_t failures{};
};

void print_port_error(const char* const operation,
                      const int result) noexcept {
    std::fprintf(
        stderr,
        "%s failed: %s\n",
        operation,
        rte_strerror(result < 0 ? -result : result));
}

pulsebook::EngineConfig make_engine_config() noexcept {
    pulsebook::EngineConfig config{};

    config.instrument_id = 77;
    config.strategy.imbalance_threshold_bps = 6000;
    config.strategy.order_quantity = 10;

    config.risk.max_order_quantity = 100;
    config.risk.max_absolute_position = 100'000'000;
    config.risk.max_notional_ticks = 15'000'000'000'000ULL;
    config.risk.max_outstanding_orders = 64;

    return config;
}

bool configure_port(const std::uint16_t port_id,
                    rte_mempool* const mempool) noexcept {
    rte_eth_conf configuration{};

    int result = rte_eth_dev_configure(
        port_id,
        kRxQueues,
        kTxQueues,
        &configuration);

    if (result < 0) {
        print_port_error("rte_eth_dev_configure", result);
        return false;
    }

    result = rte_eth_rx_queue_setup(
        port_id,
        0,
        kRxDescriptors,
        rte_socket_id(),
        nullptr,
        mempool);

    if (result < 0) {
        print_port_error("rte_eth_rx_queue_setup", result);
        return false;
    }

    result = rte_eth_tx_queue_setup(
        port_id,
        0,
        kTxDescriptors,
        rte_socket_id(),
        nullptr);

    if (result < 0) {
        print_port_error("rte_eth_tx_queue_setup", result);
        return false;
    }

    result = rte_eth_dev_start(port_id);

    if (result < 0) {
        print_port_error("rte_eth_dev_start", result);
        return false;
    }

    return true;
}

pulsebook::wire::EthernetEnvelope response_envelope(
    const pulsebook::wire::EthernetEnvelope& request) noexcept {
    pulsebook::wire::EthernetEnvelope response{};
    response.destination = request.source;
    response.source = request.destination;
    return response;
}

bool create_and_transmit_order(
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

    void* const payload = rte_pktmbuf_append(
        packet,
        static_cast<std::uint16_t>(frame.size()));

    if (payload == nullptr) {
        rte_pktmbuf_free(packet);
        return false;
    }

    std::memcpy(payload, frame.data(), frame.size());

    rte_mbuf* packets[] = {packet};

    if (rte_eth_tx_burst(port_id, 0, packets, 1) != 1) {
        rte_pktmbuf_free(packet);
        return false;
    }

    return true;
}

void print_latency_summary(
    const pulsebook::dpdk::LatencySummary& summary) {
    std::printf("  Samples:              %zu\n", summary.samples);
    std::printf("  Mean latency:         %.3f ns\n", summary.mean_ns);
    std::printf("  p50 latency:          %.3f ns\n", summary.p50_ns);
    std::printf("  p95 latency:          %.3f ns\n", summary.p95_ns);
    std::printf("  p99 latency:          %.3f ns\n", summary.p99_ns);
    std::printf("  p99.9 latency:        %.3f ns\n", summary.p999_ns);
    std::printf("  Maximum latency:      %.3f ns\n", summary.maximum_ns);
}

}  // namespace

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    const std::uint64_t tsc_hz = rte_get_tsc_hz();

    if (tsc_hz == 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: invalid TSC frequency\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    const std::uint16_t available_ports = rte_eth_dev_count_avail();

    if (available_ports == 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: no AF_PACKET DPDK port available\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    std::uint16_t port_id{};
    bool found_port = false;
    std::uint16_t current_port{};

    RTE_ETH_FOREACH_DEV(current_port) {
        port_id = current_port;
        found_port = true;
        break;
    }

    if (!found_port) {
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: port enumeration failed\n");
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
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: mempool creation failed: %s\n",
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

    pulsebook::dpdk::DpdkLatencyRecorder recorder(
        kMeasuredOrders,
        tsc_hz);

    ProcessingCounters counters{};
    std::uint32_t outbound_sequence = 5'000'001;
    bool success = true;

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 9C - AF_PACKET Latency Engine\n");
    std::printf("============================================================\n");
    std::printf("DPDK version:              %s\n", rte_version());
    std::printf("Main lcore:                %u\n", rte_lcore_id());
    std::printf("TSC frequency:             %llu Hz\n",
                static_cast<unsigned long long>(tsc_hz));
    std::printf("Linux interface:           pb_eng\n");
    std::printf("Virtual transport:         Linux veth\n");
    std::printf("PMD:                       AF_PACKET\n");
    std::printf("Seed packets expected:     %zu\n", kSeedPackets);
    std::printf("Warmup orders expected:    %zu\n", kWarmupOrders);
    std::printf("Measured orders expected:  %zu\n", kMeasuredOrders);
    std::printf("Waiting for benchmark generator...\n");
    std::fflush(stdout);

    const std::uint64_t deadline =
        rte_get_timer_cycles() +
        rte_get_timer_hz() * kTimeoutSeconds;

    while (success &&
           counters.measured_orders < kMeasuredOrders &&
           rte_get_timer_cycles() < deadline) {
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
            ++counters.received_packets;

            const std::uint64_t start_cycles = rte_rdtsc_precise();

            const auto* const data =
                rte_pktmbuf_mtod(packet, const std::byte*);

            const auto bytes = std::span<const std::byte>(
                data,
                static_cast<std::size_t>(
                    rte_pktmbuf_data_len(packet)));

            pulsebook::wire::EthernetEnvelope received_envelope{};
            pulsebook::wire::MarketDataMessage market_message{};

            const auto decode_result =
                pulsebook::wire::decode_market_data_frame(
                    bytes,
                    received_envelope,
                    market_message);

            rte_pktmbuf_free(packet);

            if (decode_result !=
                pulsebook::wire::FrameDecodeError::none) {
                ++counters.invalid_packets;
                continue;
            }

            ++counters.valid_market_packets;

            pulsebook::MarketUpdate update{};

            if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
                    market_message,
                    update)) {
                ++counters.failures;
                success = false;
                break;
            }

            const pulsebook::EngineResult result =
                engine.on_market_update(update);

            if (counters.valid_market_packets <= kSeedPackets) {
                if (result.status != pulsebook::EngineStatus::no_order) {
                    std::fprintf(
                        stderr,
                        "Seed packet unexpectedly emitted an order\n");
                    ++counters.failures;
                    success = false;
                    break;
                }

                ++counters.seed_no_signal_events;
                continue;
            }

            if (result.status != pulsebook::EngineStatus::order_emitted ||
                result.signal != pulsebook::Signal::buy) {
                std::fprintf(
                    stderr,
                    "Trigger packet did not emit expected BUY order\n");
                ++counters.failures;
                success = false;
                break;
            }

            pulsebook::OrderRequest order{};

            if (!engine.pop_order(order) ||
                !create_and_transmit_order(
                    port_id,
                    mempool,
                    response_envelope(received_envelope),
                    order,
                    outbound_sequence)) {
                std::fprintf(
                    stderr,
                    "Outbound order transmission failed\n");
                ++counters.failures;
                success = false;
                break;
            }

            const std::uint64_t end_cycles = rte_rdtsc_precise();
            const std::uint64_t elapsed_cycles =
                end_cycles - start_cycles;

            engine.acknowledge_one_order();

            const std::uint64_t completed_order_count =
                counters.warmup_orders +
                counters.measured_orders;

            if (completed_order_count < kWarmupOrders) {
                ++counters.warmup_orders;
            } else {
                if (!recorder.record_cycles(elapsed_cycles)) {
                    ++counters.failures;
                    success = false;
                    break;
                }

                ++counters.measured_orders;
            }

            if (counters.measured_orders >= kMeasuredOrders) {
                break;
            }
        }
    }

    if (counters.seed_no_signal_events != kSeedPackets ||
        counters.warmup_orders != kWarmupOrders ||
        counters.measured_orders != kMeasuredOrders ||
        counters.failures != 0) {
        success = false;
    }

    rte_eth_stats stats{};
    rte_eth_stats_get(port_id, &stats);

    if (success) {
        const auto summary = recorder.summarize();

        std::printf("\n============================================================\n");
        std::printf("PulseBook Phase 9C Benchmark Result\n");
        std::printf("============================================================\n");
        std::printf("Measured boundary:\n");
        std::printf("  Start: AF_PACKET rte_eth_rx_burst() returned packet\n");
        std::printf("  End:   AF_PACKET rte_eth_tx_burst() accepted order\n");
        std::printf("\n");
        std::printf("AF_PACKET application-side BUY packet path:\n");
        print_latency_summary(summary);
        std::printf("\n");
        std::printf("Counters:\n");
        std::printf("  Valid market packets:    %llu\n",
                    static_cast<unsigned long long>(
                        counters.valid_market_packets));
        std::printf("  Seed no-signal events:   %llu\n",
                    static_cast<unsigned long long>(
                        counters.seed_no_signal_events));
        std::printf("  Warmup orders:           %llu\n",
                    static_cast<unsigned long long>(
                        counters.warmup_orders));
        std::printf("  Measured orders:         %llu\n",
                    static_cast<unsigned long long>(
                        counters.measured_orders));
        std::printf("  Invalid filtered frames: %llu\n",
                    static_cast<unsigned long long>(
                        counters.invalid_packets));
        std::printf("  Failures:                %llu\n",
                    static_cast<unsigned long long>(
                        counters.failures));
        std::printf("  PMD RX packets:          %llu\n",
                    static_cast<unsigned long long>(stats.ipackets));
        std::printf("  PMD TX packets:          %llu\n",
                    static_cast<unsigned long long>(stats.opackets));
        std::printf("\n");
        std::printf("Classification:\n");
        std::printf("  Kernel-backed DPDK AF_PACKET application-side latency.\n");
        std::printf("  This is not VFIO hardware-bypass, physical wire latency,\n");
        std::printf("  or exchange round-trip latency.\n");
        std::printf("\nStatus:                    OK\n");

        /*
         * Give the kernel-backed peer time to receive the final enqueued
         * order before the AF_PACKET port is closed.
         */
        rte_delay_ms(100);
    } else {
        std::fprintf(
            stderr,
            "PulseBook Phase 9C failure: benchmark did not complete correctly\n");
    }

    const int stop_result = rte_eth_dev_stop(port_id);
    if (stop_result < 0) {
        print_port_error("rte_eth_dev_stop", stop_result);
    }

    const int close_result = rte_eth_dev_close(port_id);
    if (close_result < 0) {
        print_port_error("rte_eth_dev_close", close_result);
    }

    rte_mempool_free(mempool);

    const int cleanup_result = rte_eal_cleanup();

    if (!success || cleanup_result != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
