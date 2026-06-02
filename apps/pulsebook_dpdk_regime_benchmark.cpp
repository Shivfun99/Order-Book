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
inline constexpr std::size_t kWarmupEventsPerRegime = 100'000;
inline constexpr std::size_t kMeasuredEventsPerRegime = 1'000'000;

enum class Regime : std::uint8_t {
    no_signal = 0,
    buy,
    sell,
};

struct RegimeCounters {
    std::uint64_t market_packets_received{};
    std::uint64_t no_signal_decisions{};
    std::uint64_t buy_orders{};
    std::uint64_t sell_orders{};
    std::uint64_t failures{};
};

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

pulsebook::wire::MarketDataMessage make_market_message(
    const std::uint32_t sequence_number,
    const pulsebook::wire::Side side,
    const std::uint16_t level,
    const std::int64_t price_ticks,
    const std::uint32_t quantity) noexcept {
    pulsebook::wire::MarketDataMessage message{};

    message.sequence_number = sequence_number;
    message.wire_flags = 0;
    message.exchange_timestamp_ns =
        static_cast<std::uint64_t>(sequence_number) * 1000ULL;
    message.instrument_id = 77;
    message.price_ticks = price_ticks;
    message.quantity = quantity;
    message.side = side;
    message.action = pulsebook::wire::UpdateAction::modify;
    message.level = level;
    message.source_flags = 0;

    return message;
}

pulsebook::wire::MarketDataMessage make_regime_message(
    const Regime regime,
    const std::uint32_t sequence_number,
    const std::size_t event_index) noexcept {
    switch (regime) {
        case Regime::no_signal:
            return make_market_message(
                sequence_number,
                pulsebook::wire::Side::buy,
                0,
                100000,
                static_cast<std::uint32_t>(10U + (event_index & 1U)));

        case Regime::buy:
            return make_market_message(
                sequence_number,
                pulsebook::wire::Side::buy,
                0,
                100000,
                static_cast<std::uint32_t>(1000U + (event_index & 1U)));

        case Regime::sell:
            return make_market_message(
                sequence_number,
                pulsebook::wire::Side::sell,
                0,
                100100,
                static_cast<std::uint32_t>(1000U + (event_index & 1U)));
    }

    return {};
}

bool seed_balanced_book(
    pulsebook::dpdk::LatencyVirtualLink& link,
    pulsebook::TradingEngine<64>& engine,
    std::uint32_t& sequence_number) noexcept {
    const auto envelope = make_market_envelope();

    for (std::uint16_t level = 0; level < 5; ++level) {
        const auto message = make_market_message(
            sequence_number++,
            pulsebook::wire::Side::sell,
            level,
            100100 + level,
            10);

        pulsebook::wire::EthernetFrame frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                envelope,
                message,
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
        const auto message = make_market_message(
            sequence_number++,
            pulsebook::wire::Side::buy,
            level,
            100000 - level,
            10);

        pulsebook::wire::EthernetFrame frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                envelope,
                message,
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

bool validate_order(
    const pulsebook::wire::EthernetFrame& frame,
    const Regime regime) noexcept {
    pulsebook::wire::EthernetEnvelope envelope{};
    pulsebook::wire::OutboundOrderMessage order{};

    if (pulsebook::wire::decode_order_frame(
            std::span<const std::byte>(frame),
            envelope,
            order) !=
        pulsebook::wire::FrameDecodeError::none) {
        return false;
    }

    if (order.instrument_id != 77 || order.quantity != 10) {
        return false;
    }

    if (regime == Regime::buy) {
        return order.side == pulsebook::wire::Side::buy &&
               order.price_ticks == 100100;
    }

    if (regime == Regime::sell) {
        return order.side == pulsebook::wire::Side::sell &&
               order.price_ticks == 100000;
    }

    return false;
}

bool run_regime_iteration(
    pulsebook::dpdk::LatencyVirtualLink& link,
    pulsebook::TradingEngine<64>& engine,
    const Regime regime,
    const std::size_t event_index,
    std::uint32_t& incoming_sequence,
    std::uint32_t& outbound_sequence,
    std::uint64_t& measured_cycles,
    const bool validate_output,
    RegimeCounters& counters) noexcept {
    const auto message = make_regime_message(
        regime,
        incoming_sequence++,
        event_index);

    pulsebook::wire::EthernetFrame incoming_frame{};

    if (!pulsebook::wire::encode_market_data_frame(
            make_market_envelope(),
            message,
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

    pulsebook::wire::EthernetEnvelope decoded_envelope{};
    pulsebook::wire::MarketDataMessage decoded_message{};

    if (pulsebook::wire::decode_market_data_frame(
            input_bytes,
            decoded_envelope,
            decoded_message) !=
        pulsebook::wire::FrameDecodeError::none) {
        rte_pktmbuf_free(rx_packet);
        ++counters.failures;
        return false;
    }

    rte_pktmbuf_free(rx_packet);
    ++counters.market_packets_received;

    pulsebook::MarketUpdate update{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
            decoded_message,
            update)) {
        ++counters.failures;
        return false;
    }

    const pulsebook::EngineResult result =
        engine.on_market_update(update);

    if (regime == Regime::no_signal) {
        if (result.status != pulsebook::EngineStatus::no_order ||
            result.signal != pulsebook::Signal::no_signal) {
            ++counters.failures;
            return false;
        }

        const std::uint64_t end_cycles = rte_rdtsc_precise();
        measured_cycles = end_cycles - start_cycles;
        ++counters.no_signal_decisions;
        return true;
    }

    const pulsebook::Signal expected_signal =
        regime == Regime::buy
            ? pulsebook::Signal::buy
            : pulsebook::Signal::sell;

    if (result.status != pulsebook::EngineStatus::order_emitted ||
        result.signal != expected_signal) {
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
            make_order_envelope(),
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

    pulsebook::wire::EthernetFrame received_order_frame{};

    if (!link.drain_generated_order(received_order_frame)) {
        ++counters.failures;
        return false;
    }

    engine.acknowledge_one_order();

    if (validate_output &&
        !validate_order(received_order_frame, regime)) {
        ++counters.failures;
        return false;
    }

    if (regime == Regime::buy) {
        ++counters.buy_orders;
    } else {
        ++counters.sell_orders;
    }

    return true;
}

const char* regime_name(const Regime regime) noexcept {
    switch (regime) {
        case Regime::no_signal:
            return "NO_SIGNAL decision path";
        case Regime::buy:
            return "BUY packet-out path";
        case Regime::sell:
            return "SELL packet-out path";
    }

    return "UNKNOWN";
}

void print_summary(
    const Regime regime,
    const pulsebook::dpdk::LatencySummary& summary) {
    std::printf("%s:\n", regime_name(regime));
    std::printf("  Samples:              %zu\n", summary.samples);
    std::printf("  Mean latency:         %.3f ns\n", summary.mean_ns);
    std::printf("  p50 latency:          %.3f ns\n", summary.p50_ns);
    std::printf("  p95 latency:          %.3f ns\n", summary.p95_ns);
    std::printf("  p99 latency:          %.3f ns\n", summary.p99_ns);
    std::printf("  p99.9 latency:        %.3f ns\n", summary.p999_ns);
    std::printf("  Maximum latency:      %.3f ns\n", summary.maximum_ns);
}

bool benchmark_regime(
    pulsebook::dpdk::LatencyVirtualLink& link,
    const Regime regime,
    const std::uint64_t tsc_hz,
    pulsebook::dpdk::LatencySummary& output_summary,
    RegimeCounters& output_counters) {
    pulsebook::TradingEngine<64> engine(make_engine_config());

    std::uint32_t incoming_sequence =
        regime == Regime::no_signal ? 1 :
        regime == Regime::buy ? 2'000'001U : 4'000'001U;

    std::uint32_t outbound_sequence =
        regime == Regime::buy ? 6'000'001U : 8'000'001U;

    if (!seed_balanced_book(
            link,
            engine,
            incoming_sequence)) {
        return false;
    }

    for (std::size_t event = 0;
         event < kWarmupEventsPerRegime;
         ++event) {
        std::uint64_t unused_cycles{};

        const bool validate =
            regime != Regime::no_signal &&
            (event == 0 || event + 1 == kWarmupEventsPerRegime);

        if (!run_regime_iteration(
                link,
                engine,
                regime,
                event,
                incoming_sequence,
                outbound_sequence,
                unused_cycles,
                validate,
                output_counters)) {
            return false;
        }
    }

    pulsebook::dpdk::DpdkLatencyRecorder recorder(
        kMeasuredEventsPerRegime,
        tsc_hz);

    for (std::size_t event = 0;
         event < kMeasuredEventsPerRegime;
         ++event) {
        std::uint64_t cycles{};

        const bool validate =
            regime != Regime::no_signal &&
            (event == 0 || event + 1 == kMeasuredEventsPerRegime);

        if (!run_regime_iteration(
                link,
                engine,
                regime,
                event,
                incoming_sequence,
                outbound_sequence,
                cycles,
                validate,
                output_counters) ||
            !recorder.record_cycles(cycles)) {
            return false;
        }
    }

    output_summary = recorder.summarize();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 8 failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    const std::uint64_t tsc_hz = rte_get_tsc_hz();

    if (tsc_hz == 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 8 failure: invalid TSC frequency\n");
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    pulsebook::dpdk::DpdkLatencyRecorder timer_overhead(
        kTimerOverheadSamples,
        tsc_hz);

    bool success = true;

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

    pulsebook::dpdk::LatencySummary no_signal_summary{};
    pulsebook::dpdk::LatencySummary buy_summary{};
    pulsebook::dpdk::LatencySummary sell_summary{};

    RegimeCounters no_signal_counters{};
    RegimeCounters buy_counters{};
    RegimeCounters sell_counters{};

    pulsebook::dpdk::LatencyLinkStatistics virtual_stats{};

    {
        pulsebook::dpdk::LatencyVirtualLink link{};

        if (!link.initialize()) {
            std::fprintf(
                stderr,
                "PulseBook Phase 8 failure: virtual DPDK link initialization failed: %s\n",
                rte_strerror(rte_errno));
            success = false;
        }

        if (success &&
            !benchmark_regime(
                link,
                Regime::no_signal,
                tsc_hz,
                no_signal_summary,
                no_signal_counters)) {
            std::fprintf(
                stderr,
                "PulseBook Phase 8 failure: NO_SIGNAL benchmark failed\n");
            success = false;
        }

        if (success &&
            !benchmark_regime(
                link,
                Regime::buy,
                tsc_hz,
                buy_summary,
                buy_counters)) {
            std::fprintf(
                stderr,
                "PulseBook Phase 8 failure: BUY benchmark failed\n");
            success = false;
        }

        if (success &&
            !benchmark_regime(
                link,
                Regime::sell,
                tsc_hz,
                sell_summary,
                sell_counters)) {
            std::fprintf(
                stderr,
                "PulseBook Phase 8 failure: SELL benchmark failed\n");
            success = false;
        }

        if (success) {
            virtual_stats = link.statistics();
        }
    }

    if (success) {
        const auto overhead_summary = timer_overhead.summarize();

        std::printf("============================================================\n");
        std::printf("PulseBook Level 3 Phase 8 - Regime Latency Benchmark\n");
        std::printf("============================================================\n");
        std::printf("DPDK version:             %s\n", rte_version());
        std::printf("Main lcore:               %u\n", rte_lcore_id());
        std::printf("TSC frequency:            %llu Hz\n",
                    static_cast<unsigned long long>(tsc_hz));
        std::printf("Physical NIC used:        No\n");
        std::printf("Virtual PMD:              Ring PMD\n");
        std::printf("Frame size:               %zu bytes\n",
                    pulsebook::wire::kEthernetFrameBytesWithoutFcs);
        std::printf("Warmup per regime:        %zu\n",
                    kWarmupEventsPerRegime);
        std::printf("Measured per regime:      %zu\n",
                    kMeasuredEventsPerRegime);
        std::printf("\n");

        std::printf("Timing boundaries:\n");
        std::printf("  NO_SIGNAL: RX returned -> engine no-order decision\n");
        std::printf("  BUY:       RX returned -> outbound BUY TX accepted\n");
        std::printf("  SELL:      RX returned -> outbound SELL TX accepted\n");
        std::printf("\n");

        std::printf("Timer read overhead:\n");
        std::printf("  Samples:              %zu\n", overhead_summary.samples);
        std::printf("  Mean latency:         %.3f ns\n", overhead_summary.mean_ns);
        std::printf("  p50 latency:          %.3f ns\n", overhead_summary.p50_ns);
        std::printf("  p95 latency:          %.3f ns\n", overhead_summary.p95_ns);
        std::printf("  p99 latency:          %.3f ns\n", overhead_summary.p99_ns);
        std::printf("  p99.9 latency:        %.3f ns\n", overhead_summary.p999_ns);
        std::printf("  Maximum latency:      %.3f ns\n", overhead_summary.maximum_ns);
        std::printf("\n");

        print_summary(Regime::no_signal, no_signal_summary);
        std::printf("\n");
        print_summary(Regime::buy, buy_summary);
        std::printf("\n");
        print_summary(Regime::sell, sell_summary);
        std::printf("\n");

        std::printf("Regime counters including warmup:\n");
        std::printf("  NO_SIGNAL decisions:   %llu\n",
                    static_cast<unsigned long long>(
                        no_signal_counters.no_signal_decisions));
        std::printf("  BUY orders:            %llu\n",
                    static_cast<unsigned long long>(
                        buy_counters.buy_orders));
        std::printf("  SELL orders:           %llu\n",
                    static_cast<unsigned long long>(
                        sell_counters.sell_orders));
        std::printf("  Total failures:        %llu\n",
                    static_cast<unsigned long long>(
                        no_signal_counters.failures +
                        buy_counters.failures +
                        sell_counters.failures));
        std::printf("\n");

        std::printf("Virtual port counters including seeding and warmup:\n");
        std::printf("  Generator TX packets: %llu\n",
                    static_cast<unsigned long long>(virtual_stats.generator_tx));
        std::printf("  Generator RX packets: %llu\n",
                    static_cast<unsigned long long>(virtual_stats.generator_rx));
        std::printf("  Engine RX packets:    %llu\n",
                    static_cast<unsigned long long>(virtual_stats.engine_rx));
        std::printf("  Engine TX packets:    %llu\n",
                    static_cast<unsigned long long>(virtual_stats.engine_tx));
        std::printf("\n");

        std::printf("Classification:\n");
        std::printf("  Virtual DPDK application-side latency only.\n");
        std::printf("  BUY and SELL include outbound TX enqueue.\n");
        std::printf("  NO_SIGNAL ends at the no-order decision because no packet is transmitted.\n");
        std::printf("  None of these values are physical NIC wire latency or exchange RTT.\n");
        std::printf("\n");
        std::printf("Status:                   OK\n");
    }

    const int cleanup_result = rte_eal_cleanup();

    if (!success || cleanup_result != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
