#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/dpdk/engine_wire_adapter.hpp"
#include "pulsebook/engine/trading_engine.hpp"
#include "pulsebook/wire/order_packet.hpp"

namespace {

pulsebook::wire::MarketDataMessage make_market_message(
    const std::uint32_t sequence,
    const pulsebook::wire::Side side,
    const std::uint16_t level,
    const std::int64_t price_ticks,
    const std::uint32_t quantity) {
    pulsebook::wire::MarketDataMessage message{};
    message.sequence_number = sequence;
    message.exchange_timestamp_ns = sequence * 1000ULL;
    message.instrument_id = 77;
    message.price_ticks = price_ticks;
    message.quantity = quantity;
    message.side = side;
    message.action = pulsebook::wire::UpdateAction::modify;
    message.level = level;
    return message;
}

pulsebook::EngineResult send_to_engine(
    pulsebook::TradingEngine<16>& engine,
    const pulsebook::wire::MarketDataMessage& wire_message) {
    pulsebook::MarketUpdate update{};

    assert(pulsebook::dpdk::EngineWireAdapter::to_market_update(
        wire_message,
        update));

    return engine.on_market_update(update);
}

}  // namespace

int main() {
    pulsebook::EngineConfig config{};
    config.instrument_id = 77;
    config.strategy.imbalance_threshold_bps = 6000;
    config.strategy.order_quantity = 10;
    config.risk.max_order_quantity = 100;
    config.risk.max_absolute_position = 1000;
    config.risk.max_notional_ticks = 1'000'000'000ULL;

    pulsebook::TradingEngine<16> engine(config);

    std::uint32_t sequence = 1;

    for (std::uint16_t level = 0; level < 5; ++level) {
        const auto result = send_to_engine(
            engine,
            make_market_message(
                sequence++,
                pulsebook::wire::Side::sell,
                level,
                100100 + level,
                10));

        assert(result.status == pulsebook::EngineStatus::no_order);
    }

    for (std::uint16_t level = 0; level < 5; ++level) {
        const auto result = send_to_engine(
            engine,
            make_market_message(
                sequence++,
                pulsebook::wire::Side::buy,
                level,
                100000 - level,
                10));

        assert(result.status == pulsebook::EngineStatus::no_order);
    }

    const auto trigger_result = send_to_engine(
        engine,
        make_market_message(
            sequence++,
            pulsebook::wire::Side::buy,
            0,
            100000,
            1000));

    assert(trigger_result.status == pulsebook::EngineStatus::order_emitted);
    assert(trigger_result.signal == pulsebook::Signal::buy);
    assert(trigger_result.imbalance_bps >= 6000);

    pulsebook::OrderRequest emitted_order{};
    assert(engine.pop_order(emitted_order));

    static_assert(sizeof(pulsebook::OrderRequest) == 32);

    assert(emitted_order.client_order_id == 1);
    assert(emitted_order.instrument_id == 77);
    assert(emitted_order.price_ticks == 100100);
    assert(emitted_order.quantity == 10);
    assert(emitted_order.side == pulsebook::Side::buy);
    assert(emitted_order.order_kind == pulsebook::OrderKind::limit);

    pulsebook::wire::OutboundOrderMessage wire_order{};

    assert(pulsebook::dpdk::EngineWireAdapter::to_wire_order(
        emitted_order,
        5001,
        wire_order));

    pulsebook::wire::WireMessage encoded{};
    assert(pulsebook::wire::encode_order(wire_order, encoded));

    pulsebook::wire::OutboundOrderMessage decoded{};

    assert(pulsebook::wire::decode_order(
               std::span<const std::byte>(encoded),
               decoded) ==
           pulsebook::wire::DecodeError::none);

    assert(decoded.sequence_number == 5001);
    assert(decoded.client_order_id == emitted_order.client_order_id);
    assert(decoded.instrument_id == emitted_order.instrument_id);
    assert(decoded.price_ticks == emitted_order.price_ticks);
    assert(decoded.quantity == emitted_order.quantity);
    assert(decoded.side == pulsebook::wire::Side::buy);
    assert(decoded.reserved == emitted_order.source_sequence);

    return 0;
}
