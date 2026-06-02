#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_version.h>

#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/dpdk/virtual_link.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/endian.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

struct ScenarioCounters {
    std::uint32_t market_frames_injected{};
    std::uint32_t valid_frames_decoded{};
    std::uint32_t invalid_frames_dropped{};
    std::uint32_t no_signal_results{};
    std::uint32_t buy_orders{};
    std::uint32_t sell_orders{};
    std::uint32_t risk_rejected{};
    std::uint32_t invalid_updates{};
};

pulsebook::EngineConfig make_config(
    const pulsebook::Quantity max_order_quantity = 100) noexcept {
    pulsebook::EngineConfig config{};
    config.instrument_id = 77;
    config.strategy.imbalance_threshold_bps = 6000;
    config.strategy.order_quantity = 10;
    config.risk.max_order_quantity = max_order_quantity;
    config.risk.max_absolute_position = 1000;
    config.risk.max_notional_ticks = 1'000'000'000ULL;
    config.risk.max_outstanding_orders = 64;
    return config;
}

pulsebook::wire::EthernetEnvelope market_envelope() noexcept {
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

pulsebook::wire::EthernetEnvelope order_envelope() noexcept {
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
    const std::uint32_t sequence_number,
    const pulsebook::wire::Side side,
    const std::uint16_t level,
    const std::int64_t price_ticks,
    const std::uint32_t quantity) noexcept {
    pulsebook::wire::MarketDataMessage message{};
    message.sequence_number = sequence_number;
    message.exchange_timestamp_ns =
        static_cast<std::uint64_t>(sequence_number) * 1000ULL;
    message.instrument_id = 77;
    message.price_ticks = price_ticks;
    message.quantity = quantity;
    message.side = side;
    message.action = pulsebook::wire::UpdateAction::modify;
    message.level = level;
    return message;
}

class ScenarioHarness {
public:
    explicit ScenarioHarness(
        pulsebook::dpdk::VirtualLink& link) noexcept
        : link_(link) {
    }

    [[nodiscard]] bool send_market_update(
        pulsebook::TradingEngine<64>& engine,
        const pulsebook::wire::MarketDataMessage& source_message,
        const pulsebook::EngineStatus expected_status,
        const bool expect_order = false,
        const pulsebook::wire::Side expected_side =
            pulsebook::wire::Side::buy,
        const pulsebook::PriceTicks expected_price_ticks = 0) noexcept {
        pulsebook::wire::EthernetFrame source_frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                market_envelope(),
                source_message,
                source_frame)) {
            return false;
        }

        if (!link_.send_generator_to_engine(source_frame)) {
            return false;
        }

        ++counters_.market_frames_injected;

        pulsebook::wire::EthernetFrame received_frame{};

        if (!link_.receive_at_engine(received_frame)) {
            return false;
        }

        pulsebook::wire::EthernetEnvelope received_envelope{};
        pulsebook::wire::MarketDataMessage decoded_message{};

        if (pulsebook::wire::decode_market_data_frame(
                std::span<const std::byte>(received_frame),
                received_envelope,
                decoded_message) !=
            pulsebook::wire::FrameDecodeError::none) {
            return false;
        }

        ++counters_.valid_frames_decoded;

        pulsebook::MarketUpdate update{};

        if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
                decoded_message,
                update)) {
            return false;
        }

        const pulsebook::EngineResult result =
            engine.on_market_update(update);

        if (result.status != expected_status) {
            return false;
        }

        if (result.status == pulsebook::EngineStatus::no_order) {
            ++counters_.no_signal_results;
            return !expect_order;
        }

        if (result.status == pulsebook::EngineStatus::risk_rejected) {
            ++counters_.risk_rejected;
            return !expect_order;
        }

        if (result.status == pulsebook::EngineStatus::invalid_update) {
            ++counters_.invalid_updates;
            return !expect_order;
        }

        if (result.status != pulsebook::EngineStatus::order_emitted ||
            !expect_order) {
            return false;
        }

        pulsebook::OrderRequest order{};

        if (!engine.pop_order(order)) {
            return false;
        }

        pulsebook::wire::OutboundOrderMessage wire_order{};

        if (!pulsebook::dpdk::EngineWireAdapter::to_wire_order(
                order,
                outbound_sequence_++,
                wire_order)) {
            return false;
        }

        pulsebook::wire::EthernetFrame order_frame{};

        if (!pulsebook::wire::encode_order_frame(
                order_envelope(),
                wire_order,
                order_frame)) {
            return false;
        }

        if (!link_.send_engine_to_generator(order_frame)) {
            return false;
        }

        pulsebook::wire::EthernetFrame received_order_frame{};

        if (!link_.receive_at_generator(received_order_frame)) {
            return false;
        }

        pulsebook::wire::EthernetEnvelope decoded_order_envelope{};
        pulsebook::wire::OutboundOrderMessage decoded_order{};

        if (pulsebook::wire::decode_order_frame(
                std::span<const std::byte>(received_order_frame),
                decoded_order_envelope,
                decoded_order) !=
            pulsebook::wire::FrameDecodeError::none) {
            return false;
        }

        if (decoded_order.side != expected_side ||
            decoded_order.price_ticks != expected_price_ticks ||
            decoded_order.quantity != 10 ||
            decoded_order.instrument_id != 77) {
            return false;
        }

        if (expected_side == pulsebook::wire::Side::buy) {
            ++counters_.buy_orders;
        } else {
            ++counters_.sell_orders;
        }

        return true;
    }

    [[nodiscard]] bool send_invalid_ether_type(
        const std::uint32_t sequence_number) noexcept {
        const auto message = make_message(
            sequence_number,
            pulsebook::wire::Side::buy,
            0,
            100000,
            10);

        pulsebook::wire::EthernetFrame frame{};

        if (!pulsebook::wire::encode_market_data_frame(
                market_envelope(),
                message,
                frame)) {
            return false;
        }

        pulsebook::wire::store_be16(
            std::span<std::byte>(frame),
            pulsebook::wire::kEtherTypeOffset,
            0x0800U);

        if (!link_.send_generator_to_engine(frame)) {
            return false;
        }

        ++counters_.market_frames_injected;

        pulsebook::wire::EthernetFrame received_frame{};

        if (!link_.receive_at_engine(received_frame)) {
            return false;
        }

        pulsebook::wire::EthernetEnvelope envelope{};
        pulsebook::wire::MarketDataMessage decoded{};

        const auto error = pulsebook::wire::decode_market_data_frame(
            std::span<const std::byte>(received_frame),
            envelope,
            decoded);

        if (error != pulsebook::wire::FrameDecodeError::wrong_ether_type) {
            return false;
        }

        ++counters_.invalid_frames_dropped;
        return true;
    }

    [[nodiscard]] const ScenarioCounters& counters() const noexcept {
        return counters_;
    }

private:
    pulsebook::dpdk::VirtualLink& link_;
    ScenarioCounters counters_{};
    std::uint32_t outbound_sequence_{5001};
};

bool seed_balanced_book(
    ScenarioHarness& harness,
    pulsebook::TradingEngine<64>& engine,
    std::uint32_t& sequence_number) noexcept {
    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!harness.send_market_update(
                engine,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::sell,
                    level,
                    100100 + level,
                    10),
                pulsebook::EngineStatus::no_order)) {
            return false;
        }
    }

    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!harness.send_market_update(
                engine,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::buy,
                    level,
                    100000 - level,
                    10),
                pulsebook::EngineStatus::no_order)) {
            return false;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        std::fprintf(
            stderr,
            "PulseBook Phase 7 failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    bool success = true;

    {
        pulsebook::dpdk::VirtualLink link{};

        if (!link.initialize()) {
            std::fprintf(
                stderr,
                "PulseBook Phase 7 failure: virtual link initialization failed: %s\n",
                rte_strerror(rte_errno));
            success = false;
        } else {
            ScenarioHarness harness(link);
            std::uint32_t sequence_number = 1;

            pulsebook::TradingEngine<64> no_signal_engine(make_config());

            success = seed_balanced_book(
                harness,
                no_signal_engine,
                sequence_number);

            pulsebook::TradingEngine<64> buy_engine(make_config());

            if (success) {
                success = seed_balanced_book(
                    harness,
                    buy_engine,
                    sequence_number);
            }

            if (success) {
                success = harness.send_market_update(
                    buy_engine,
                    make_message(
                        sequence_number++,
                        pulsebook::wire::Side::buy,
                        0,
                        100000,
                        1000),
                    pulsebook::EngineStatus::order_emitted,
                    true,
                    pulsebook::wire::Side::buy,
                    100100);
            }

            pulsebook::TradingEngine<64> sell_engine(make_config());

            if (success) {
                success = seed_balanced_book(
                    harness,
                    sell_engine,
                    sequence_number);
            }

            if (success) {
                success = harness.send_market_update(
                    sell_engine,
                    make_message(
                        sequence_number++,
                        pulsebook::wire::Side::sell,
                        0,
                        100100,
                        1000),
                    pulsebook::EngineStatus::order_emitted,
                    true,
                    pulsebook::wire::Side::sell,
                    100000);
            }

            pulsebook::TradingEngine<64> risk_engine(make_config(5));

            if (success) {
                success = seed_balanced_book(
                    harness,
                    risk_engine,
                    sequence_number);
            }

            if (success) {
                success = harness.send_market_update(
                    risk_engine,
                    make_message(
                        sequence_number++,
                        pulsebook::wire::Side::buy,
                        0,
                        100000,
                        1000),
                    pulsebook::EngineStatus::risk_rejected);
            }

            if (success) {
                success = harness.send_invalid_ether_type(
                    sequence_number++);
            }

            const auto& counters = harness.counters();
            const auto stats = link.statistics();

            const bool counters_correct =
                counters.market_frames_injected == 44 &&
                counters.valid_frames_decoded == 43 &&
                counters.invalid_frames_dropped == 1 &&
                counters.no_signal_results == 40 &&
                counters.buy_orders == 1 &&
                counters.sell_orders == 1 &&
                counters.risk_rejected == 1 &&
                counters.invalid_updates == 0 &&
                stats.generator_tx == 44 &&
                stats.generator_rx == 2 &&
                stats.engine_rx == 44 &&
                stats.engine_tx == 2;

            if (!counters_correct) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 7 failure: scenario counters are incorrect\n");
                success = false;
            }

            if (success) {
                std::printf("============================================================\n");
                std::printf("PulseBook Level 3 Phase 7 - DPDK Scenario Validation\n");
                std::printf("============================================================\n");
                std::printf("DPDK version:              %s\n", rte_version());
                std::printf("Main lcore:                %u\n", rte_lcore_id());
                std::printf("Physical NIC used:         No\n");
                std::printf("Virtual PMD:               Ring PMD\n");
                std::printf("\n");
                std::printf("Scenario results:\n");
                std::printf("  NO_SIGNAL scenario:      PASSED\n");
                std::printf("  BUY scenario:            PASSED\n");
                std::printf("  SELL scenario:           PASSED\n");
                std::printf("  RISK_REJECT scenario:    PASSED\n");
                std::printf("  INVALID_FRAME scenario:  PASSED\n");
                std::printf("\n");
                std::printf("Packet counters:\n");
                std::printf("  Market frames injected:  %u\n",
                            counters.market_frames_injected);
                std::printf("  Valid frames decoded:    %u\n",
                            counters.valid_frames_decoded);
                std::printf("  Invalid frames dropped:  %u\n",
                            counters.invalid_frames_dropped);
                std::printf("  No-signal results:       %u\n",
                            counters.no_signal_results);
                std::printf("  BUY orders emitted:      %u\n",
                            counters.buy_orders);
                std::printf("  SELL orders emitted:     %u\n",
                            counters.sell_orders);
                std::printf("  Risk rejected:           %u\n",
                            counters.risk_rejected);
                std::printf("\n");
                std::printf("Virtual port counters:\n");
                std::printf("  Generator TX packets:    %llu\n",
                            static_cast<unsigned long long>(stats.generator_tx));
                std::printf("  Generator RX packets:    %llu\n",
                            static_cast<unsigned long long>(stats.generator_rx));
                std::printf("  Engine RX packets:       %llu\n",
                            static_cast<unsigned long long>(stats.engine_rx));
                std::printf("  Engine TX packets:       %llu\n",
                            static_cast<unsigned long long>(stats.engine_tx));
                std::printf("\n");
                std::printf("Status:                    OK\n");
            }
        }
    }

    const int cleanup_result = rte_eal_cleanup();

    if (!success || cleanup_result != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
