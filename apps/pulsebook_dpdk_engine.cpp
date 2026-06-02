#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_version.h>

#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/dpdk/virtual_link.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

struct ProcessingCounters {
    std::uint32_t market_packets_received{};
    std::uint32_t no_signal_events{};
    std::uint32_t orders_emitted{};
    std::uint32_t invalid_updates{};
    std::uint32_t risk_rejected{};
    std::uint32_t outbox_full{};
};

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

bool process_packet(
    pulsebook::dpdk::VirtualLink& link,
    pulsebook::TradingEngine<64>& engine,
    const pulsebook::wire::MarketDataMessage& source_message,
    std::uint32_t& outbound_sequence,
    ProcessingCounters& counters) noexcept {
    pulsebook::wire::EthernetFrame source_frame{};

    if (!pulsebook::wire::encode_market_data_frame(
            market_envelope(),
            source_message,
            source_frame)) {
        return false;
    }

    if (!link.send_generator_to_engine(source_frame)) {
        return false;
    }

    pulsebook::wire::EthernetFrame received_frame{};

    if (!link.receive_at_engine(received_frame)) {
        return false;
    }

    pulsebook::wire::EthernetEnvelope received_envelope{};
    pulsebook::wire::MarketDataMessage decoded_wire_message{};

    if (pulsebook::wire::decode_market_data_frame(
            std::span<const std::byte>(received_frame),
            received_envelope,
            decoded_wire_message) !=
        pulsebook::wire::FrameDecodeError::none) {
        return false;
    }

    ++counters.market_packets_received;

    pulsebook::MarketUpdate update{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_market_update(
            decoded_wire_message,
            update)) {
        ++counters.invalid_updates;
        return true;
    }

    const pulsebook::EngineResult result =
        engine.on_market_update(update);

    switch (result.status) {
        case pulsebook::EngineStatus::no_order:
            ++counters.no_signal_events;
            return true;

        case pulsebook::EngineStatus::invalid_update:
            ++counters.invalid_updates;
            return true;

        case pulsebook::EngineStatus::risk_rejected:
            ++counters.risk_rejected;
            return true;

        case pulsebook::EngineStatus::outbox_full:
            ++counters.outbox_full;
            return true;

        case pulsebook::EngineStatus::order_emitted:
            break;
    }

    pulsebook::OrderRequest emitted_order{};

    if (!engine.pop_order(emitted_order)) {
        return false;
    }

    pulsebook::wire::OutboundOrderMessage wire_order{};

    if (!pulsebook::dpdk::EngineWireAdapter::to_wire_order(
            emitted_order,
            outbound_sequence++,
            wire_order)) {
        return false;
    }

    pulsebook::wire::EthernetFrame outbound_frame{};

    if (!pulsebook::wire::encode_order_frame(
            order_envelope(),
            wire_order,
            outbound_frame)) {
        return false;
    }

    if (!link.send_engine_to_generator(outbound_frame)) {
        return false;
    }

    ++counters.orders_emitted;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        std::fprintf(stderr, "PulseBook Phase 5 failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 5 - DPDK Trading Engine Path\n");
    std::printf("============================================================\n");
    std::printf("DPDK version:              %s\n", rte_version());
    std::printf("Main lcore:                %u\n", rte_lcore_id());
    std::printf("Physical ports in use:     0\n");

    bool success = true;

    {
        pulsebook::dpdk::VirtualLink link{};

        if (!link.initialize()) {
            std::fprintf(
                stderr,
                "PulseBook Phase 5 failure: virtual link initialization failed: %s\n",
                rte_strerror(rte_errno));
            success = false;
        } else {
            pulsebook::EngineConfig config{};
            config.instrument_id = 77;
            config.strategy.imbalance_threshold_bps = 6000;
            config.strategy.order_quantity = 10;
            config.risk.max_order_quantity = 100;
            config.risk.max_absolute_position = 1000;
            config.risk.max_notional_ticks = 1'000'000'000ULL;
            config.risk.max_outstanding_orders = 64;

            pulsebook::TradingEngine<64> engine(config);

            ProcessingCounters counters{};
            std::uint32_t incoming_sequence = 1;
            std::uint32_t outbound_sequence = 5001;

            for (std::uint16_t level = 0;
                 level < 5 && success;
                 ++level) {
                success = process_packet(
                    link,
                    engine,
                    make_market_message(
                        incoming_sequence++,
                        pulsebook::wire::Side::sell,
                        level,
                        100100 + level,
                        10),
                    outbound_sequence,
                    counters);
            }

            for (std::uint16_t level = 0;
                 level < 5 && success;
                 ++level) {
                success = process_packet(
                    link,
                    engine,
                    make_market_message(
                        incoming_sequence++,
                        pulsebook::wire::Side::buy,
                        level,
                        100000 - level,
                        10),
                    outbound_sequence,
                    counters);
            }

            if (success && counters.orders_emitted != 0) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 5 failure: balanced initialization emitted an order\n");
                success = false;
            }

            if (success) {
                success = process_packet(
                    link,
                    engine,
                    make_market_message(
                        incoming_sequence++,
                        pulsebook::wire::Side::buy,
                        0,
                        100000,
                        1000),
                    outbound_sequence,
                    counters);
            }

            pulsebook::wire::EthernetFrame generated_order_frame{};
            pulsebook::wire::EthernetEnvelope generated_order_envelope{};
            pulsebook::wire::OutboundOrderMessage generated_order{};

            if (success &&
                !link.receive_at_generator(generated_order_frame)) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 5 failure: generated order packet not received\n");
                success = false;
            }

            if (success &&
                pulsebook::wire::decode_order_frame(
                    std::span<const std::byte>(generated_order_frame),
                    generated_order_envelope,
                    generated_order) !=
                pulsebook::wire::FrameDecodeError::none) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 5 failure: generated order packet is invalid\n");
                success = false;
            }

            if (success &&
                (counters.orders_emitted != 1 ||
                 generated_order.client_order_id != 1 ||
                 generated_order.instrument_id != 77 ||
                 generated_order.price_ticks != 100100 ||
                 generated_order.quantity != 10 ||
                 generated_order.side != pulsebook::wire::Side::buy)) {
                std::fprintf(
                    stderr,
                    "PulseBook Phase 5 failure: generated order fields are incorrect\n");
                success = false;
            }

            if (success) {
                const auto stats = link.statistics();

                std::printf("Generator virtual port:    %u\n", link.generator_port());
                std::printf("Engine virtual port:       %u\n", link.engine_port());
                std::printf("Market packets processed:  %u\n", counters.market_packets_received);
                std::printf("No-signal events:          %u\n", counters.no_signal_events);
                std::printf("Orders emitted:            %u\n", counters.orders_emitted);
                std::printf("Risk rejected:             %u\n", counters.risk_rejected);
                std::printf("Invalid updates:           %u\n", counters.invalid_updates);
                std::printf("Outbox full:               %u\n", counters.outbox_full);
                std::printf("Generated order side:      BUY\n");
                std::printf("Generated order price:     %lld\n",
                            static_cast<long long>(generated_order.price_ticks));
                std::printf("Generated order quantity:  %u\n",
                            generated_order.quantity);
                std::printf("Generator TX packets:      %llu\n",
                            static_cast<unsigned long long>(stats.generator_tx));
                std::printf("Generator RX packets:      %llu\n",
                            static_cast<unsigned long long>(stats.generator_rx));
                std::printf("Engine RX packets:         %llu\n",
                            static_cast<unsigned long long>(stats.engine_rx));
                std::printf("Engine TX packets:         %llu\n",
                            static_cast<unsigned long long>(stats.engine_tx));
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
