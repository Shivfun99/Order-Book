#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_version.h>

#include "pulsebook/dpdk/dpdk_latency_recorder.hpp"
#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/dpdk/latency_virtual_link.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

inline constexpr std::size_t kTimerOverheadSamples = 100'000;
inline constexpr std::size_t kWarmupEvents = 100'000;
inline constexpr std::size_t kMeasuredEvents = 1'000'000;

struct BenchmarkCounters {
    std::uint64_t packets_received{};
    std::uint64_t orders_emitted{};
    std::uint64_t failures{};
};

pulsebook::wire::EthernetEnvelope make_market_envelope() noexcept {
    pulsebook::wire::EthernetEnvelope envelope{};

    envelope.destination = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x20},
    };

    envelope.source = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x10},
    };

    return envelope;
}

pulsebook::wire::EthernetEnvelope make_order_envelope() noexcept {
    pulsebook::wire::EthernetEnvelope envelope{};

    envelope.destination = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x10},
    };

    envelope.source = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x20},
    };

    return envelope;
}

pulsebook::wire::MarketDataMessage make_message(
    const std::uint32_t sequence,
    const pulsebook::wire::Side side,
    const std::uint16_t level,
    const std::int64_t price_ticks,
    const std::uint32_t quantity) noexcept {
    pulsebook::wire::MarketDataMessage message{};

    message.sequence_number = sequence;
    message.wire_flags = 0;
    message.exchange_timestamp_ns =
        static_cast<std::uint64_t>(sequence) * 1000ULL;
    message.instrument_id = 77;
    message.price_ticks = price_ticks;
    message.quantity = quantity;
    message.side = side;
    message.action = pulsebook::wire::UpdateAction::modify;
    message.level = level;
    message.source_flags = 0;

    return message;
}

bool seed_balanced_book(
    pulsebook::dpdk::LatencyVirtualLink& link,
    pulsebook::TradingEngine<64>& engine,
    std::uint32_t& sequence) noexcept {
    const auto envelope = make_market_envelope();

    for (std::uint16_t level = 0; level < 5; ++level) {
        const auto wire_message = make_message(
            sequence++,
            pulsebook::wire::Side::sell,
            level,
            100100 + level,
            10);

        pulsebook::wire::EthernetFrame frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                envelope,
                wire_message,
                frame) ||
            !link.inject_market_frame(frame)) {
            return false;
        }

        rte_mbuf* packet{};

        if (!link.receive_engine_packet(packet)) {
            return false;
        }

        const auto* data =
            rte_pktmbuf_mtod(packet, const std::byte*);

        const auto bytes = std::span<const std::byte>(
            data,
            static_cast<std::size_t>(rte_pktmbuf_data_len(packet)));

        pulsebook::wire::EthernetEnvelope decoded_envelope{};
        pulsebook::wire::MarketDataMessage decoded_message{};

        const auto decode_result =
            pulsebook::wire::decode_market_data_frame(
                bytes,
                decoded_envelope,
                decoded_message);

        rte_pktmbuf_free(packet);

        if (decode_result != pulsebook::wire::FrameDecodeError::none) {
            return false;
        }

        pulsebook::MarketUpdate update{};

        if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
                decoded_message,
                update)) {
            return false;
        }

        if (engine.on_market_update(update).status !=
            pulsebook::EngineStatus::no_order) {
            return false;
        }
    }

    for (std::uint16_t level = 0; level < 5; ++level) {
        const auto wire_message = make_message(
            sequence++,
            pulsebook::wire::Side::buy,
            level,
            100000 - level,
            10);

        pulsebook::wire::EthernetFrame frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                envelope,
                wire_message,
                frame) ||
            !link.inject_market_frame(frame)) {
            return false;
        }

        rte_mbuf* packet{};

        if (!link.receive_engine_packet(packet)) {
            return false;
        }

        const auto* data =
            rte_pktmbuf_mtod(packet, const std::byte*);

        const auto bytes = std::span<const std::byte>(
            data,
            static_cast<std::size_t>(rte_pktmbuf_data_len(packet)));

        pulsebook::wire::EthernetEnvelope decoded_envelope{};
        pulsebook::wire::MarketDataMessage decoded_message{};

        const auto decode_result =
            pulsebook::wire::decode_market_data_frame(
                bytes,
                decoded_envelope,
                decoded_message);

        rte_pktmbuf_free(packet);

        if (decode_result != pulsebook::wire::FrameDecodeError::none) {
            return false;
        }

        pulsebook::MarketUpdate update{};

        if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
                decoded_message,
                update)) {
            return false;
        }

        if (engine.on_market_update(update).status !=
            pulsebook::EngineStatus::no_order) {
            return false;
        }
    }

    return true;
}

bool run_order_iteration(
    pulsebook::dpdk::LatencyVirtualLink& link,
    pulsebook::TradingEngine<64>& engine,
    const pulsebook::wire::EthernetEnvelope& market_envelope,
    const pulsebook::wire::EthernetEnvelope& order_envelope,
    std::uint32_t& incoming_sequence,
    std::uint32_t& outbound_sequence,
    std::uint64_t& measured_cycles,
    const bool validate_outbound,
    BenchmarkCounters& counters) noexcept {
    const std::uint32_t current_sequence = incoming_sequence++;
    const std::uint32_t trigger_quantity =
        1000U + (current_sequence & 1U);

    const auto market_message = make_message(
        current_sequence,
        pulsebook::wire::Side::buy,
        0,
        100000,
        trigger_quantity);

    pulsebook::wire::EthernetFrame incoming_frame{};

    if (!pulsebook::wire::encode_market_data_frame(
            market_envelope,
            market_message,
            incoming_frame) ||
        !link.inject_market_frame(incoming_frame)) {
        ++counters.failures;
        return false;
    }

    rte_mbuf* rx_packet{};

    if (!link.receive_engine_packet(rx_packet)) {
        ++counters.failures;
        return false;
    }

    const std::uint64_t start_cycles = rte_rdtsc_precise();

    const auto* input_data =
        rte_pktmbuf_mtod(rx_packet, const std::byte*);

    const auto input_bytes = std::span<const std::byte>(
        input_data,
        static_cast<std::size_t>(rte_pktmbuf_data_len(rx_packet)));

    pulsebook::wire::EthernetEnvelope received_envelope{};
    pulsebook::wire::MarketDataMessage decoded_message{};

    if (pulsebook::wire::decode_market_data_frame(
            input_bytes,
            received_envelope,
            decoded_message) !=
        pulsebook::wire::FrameDecodeError::none) {
        rte_pktmbuf_free(rx_packet);
        ++counters.failures;
        return false;
    }

    rte_pktmbuf_free(rx_packet);
    ++counters.packets_received;

    pulsebook::MarketUpdate update{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
            decoded_message,
            update)) {
        ++counters.failures;
        return false;
    }

    const pulsebook::EngineResult result =
        engine.on_market_update(update);

    if (result.status != pulsebook::EngineStatus::order_emitted ||
        result.signal != pulsebook::Signal::buy) {
        ++counters.failures;
        return false;
    }

    pulsebook::OrderRequest order{};

    if (!engine.pop_order(order)) {
        ++counters.failures;
        return false;
    }

    pulsebook::wire::OutboundOrderMessage wire_order{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_wire_order(
            order,
            outbound_sequence++,
            wire_order)) {
        ++counters.failures;
        return false;
    }

    pulsebook::wire::EthernetFrame outbound_frame{};

    if (!pulsebook::wire::encode_order_frame(
            order_envelope,
            wire_order,
            outbound_frame)) {
        ++counters.failures;
        return false;
    }

    rte_mbuf* tx_packet = rte_pktmbuf_alloc(link.mempool());

    if (tx_packet == nullptr) {
        ++counters.failures;
        return false;
    }

    void* destination = rte_pktmbuf_append(
        tx_packet,
        static_cast<std::uint16_t>(outbound_frame.size()));

    if (destination == nullptr) {
        rte_pktmbuf_free(tx_packet);
        ++counters.failures;
        return false;
    }

    std::memcpy(
        destination,
        outbound_frame.data(),
        outbound_frame.size());

    if (!link.transmit_engine_packet(tx_packet)) {
        ++counters.failures;
        return false;
    }

    const std::uint64_t end_cycles = rte_rdtsc_precise();

    measured_cycles = end_cycles - start_cycles;
    ++counters.orders_emitted;

    pulsebook::wire::EthernetFrame drained_order_frame{};

    if (!link.drain_generated_order(drained_order_frame)) {
        ++counters.failures;
        return false;
    }

    engine.acknowledge_one_order();

    if (validate_outbound) {
        pulsebook::wire::EthernetEnvelope decoded_envelope{};
        pulsebook::wire::OutboundOrderMessage decoded_order{};

        if (pulsebook::wire::decode_order_frame(
                std::span<const std::byte>(drained_order_frame),
                decoded_envelope,
                decoded_order) !=
            pulsebook::wire::FrameDecodeError::none) {
            ++counters.failures;
            return false;
        }

        if (decoded_order.instrument_id != 77 ||
            decoded_order.price_ticks != 100100 ||
            decoded_order.quantity != 10 ||
            decoded_order.side != pulsebook::wire::Side::buy) {
            ++counters.failures;
            return false;
        }
    }

    return true;
}

void print_summary(
    const char* label,
    const pulsebook::dpdk::LatencySummary& summary) {
    std::printf("%s\n", label);
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
            "PulseBook Phase 6 failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    const std::uint64_t tsc_hz = rte_get_tsc_hz();

    if (tsc_hz == 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 6 failure: invalid TSC frequency\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    bool success = true;

    pulsebook::dpdk::DpdkLatencyRecorder timer_overhead(
        kTimerOverheadSamples,
        tsc_hz);

    for (std::size_t sample = 0;
         sample < kTimerOverheadSamples;
         ++sample) {
        const std::uint64_t start = rte_rdtsc_precise();
        const std::uint64_t end = rte_rdtsc_precise();

        if (!timer_overhead.record_cycles(end - start)) {
            success = false;
            break;
        }
    }

    pulsebook::dpdk::DpdkLatencyRecorder latency_recorder(
        kMeasuredEvents,
        tsc_hz);

    BenchmarkCounters warmup_counters{};
    BenchmarkCounters measured_counters{};

    {
        pulsebook::dpdk::LatencyVirtualLink link{};

        if (!link.initialize()) {
            std::fprintf(
                stderr,
                "PulseBook Phase 6 failure: virtual DPDK link initialization failed: %s\n",
                rte_strerror(rte_errno));
            success = false;
        } else {
            pulsebook::EngineConfig config{};
            config.instrument_id = 77;
            config.strategy.imbalance_threshold_bps = 6000;
            config.strategy.order_quantity = 10;

            config.risk.max_order_quantity = 100;
            config.risk.max_absolute_position = 100'000'000;
            config.risk.max_notional_ticks = 15'000'000'000'000ULL;
            config.risk.max_outstanding_orders = 64;

            pulsebook::TradingEngine<64> engine(config);

            std::uint32_t incoming_sequence = 1;
            std::uint32_t outbound_sequence = 1;

            const auto market_envelope = make_market_envelope();
            const auto order_envelope = make_order_envelope();

            if (success &&
                !seed_balanced_book(
                    link,
                    engine,
                    incoming_sequence)) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 6 failure: book seeding failed\n");
                success = false;
            }

            for (std::size_t event = 0;
                 event < kWarmupEvents && success;
                 ++event) {
                std::uint64_t unused_cycles{};

                const bool validate =
                    event == 0 || event + 1 == kWarmupEvents;

                success = run_order_iteration(
                    link,
                    engine,
                    market_envelope,
                    order_envelope,
                    incoming_sequence,
                    outbound_sequence,
                    unused_cycles,
                    validate,
                    warmup_counters);
            }

            for (std::size_t event = 0;
                 event < kMeasuredEvents && success;
                 ++event) {
                std::uint64_t cycles{};

                const bool validate =
                    event == 0 || event + 1 == kMeasuredEvents;

                success = run_order_iteration(
                    link,
                    engine,
                    market_envelope,
                    order_envelope,
                    incoming_sequence,
                    outbound_sequence,
                    cycles,
                    validate,
                    measured_counters);

                if (success &&
                    !latency_recorder.record_cycles(cycles)) {
                    success = false;
                }
            }

            const auto stats = link.statistics();

            if (success) {
                const auto overhead_summary =
                    timer_overhead.summarize();

                const auto latency_summary =
                    latency_recorder.summarize();

                std::printf("============================================================\n");
                std::printf("PulseBook Level 3 Phase 6 - Virtual DPDK Latency Benchmark\n");
                std::printf("============================================================\n");
                std::printf("DPDK version:             %s\n", rte_version());
                std::printf("Main lcore:               %u\n", rte_lcore_id());
                std::printf("TSC frequency:            %llu Hz\n",
                            static_cast<unsigned long long>(tsc_hz));
                std::printf("Physical NIC used:        No\n");
                std::printf("Virtual PMD:              Ring PMD\n");
                std::printf("Frame size:               %zu bytes\n",
                            pulsebook::wire::kEthernetFrameBytesWithoutFcs);
                std::printf("Warmup order events:      %zu\n", kWarmupEvents);
                std::printf("Measured order events:    %zu\n", kMeasuredEvents);
                std::printf("\n");

                std::printf("Measured timing boundary:\n");
                std::printf("  Start: after engine rte_eth_rx_burst() returned packet\n");
                std::printf("  End:   after engine rte_eth_tx_burst() accepted order\n");
                std::printf("\n");

                print_summary("Timer read overhead:", overhead_summary);
                std::printf("\n");
                print_summary("Application-side DPDK packet path:", latency_summary);
                std::printf("\n");

                std::printf("Measured counters:\n");
                std::printf("  Packets received:      %llu\n",
                            static_cast<unsigned long long>(
                                measured_counters.packets_received));
                std::printf("  Orders emitted:        %llu\n",
                            static_cast<unsigned long long>(
                                measured_counters.orders_emitted));
                std::printf("  Failures:              %llu\n",
                            static_cast<unsigned long long>(
                                measured_counters.failures));
                std::printf("\n");

                std::printf("Virtual port counters including setup and warmup:\n");
                std::printf("  Generator TX packets: %llu\n",
                            static_cast<unsigned long long>(stats.generator_tx));
                std::printf("  Generator RX packets: %llu\n",
                            static_cast<unsigned long long>(stats.generator_rx));
                std::printf("  Engine RX packets:    %llu\n",
                            static_cast<unsigned long long>(stats.engine_rx));
                std::printf("  Engine TX packets:    %llu\n",
                            static_cast<unsigned long long>(stats.engine_tx));
                std::printf("\n");

                std::printf("Classification:\n");
                std::printf("  Virtual DPDK application-side packet-in to packet-out latency.\n");
                std::printf("  This is not physical NIC wire latency or exchange RTT.\n");
                std::printf("\n");
                std::printf("Status:                   OK\n");
            }
        }
    }

    const int cleanup_result = rte_eal_cleanup();

    if (!success || cleanup_result != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
