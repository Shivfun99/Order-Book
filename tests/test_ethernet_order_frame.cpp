#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/ethernet_frame.hpp"

int main() {
    using namespace pulsebook::wire;

    EthernetEnvelope input_envelope{};
    input_envelope.destination = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x10},
    };
    input_envelope.source = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x20},
    };

    OutboundOrderMessage input_order{};
    input_order.sequence_number = 202;
    input_order.wire_flags = 0x200U;
    input_order.client_order_id = 900001ULL;
    input_order.instrument_id = 7;
    input_order.price_ticks = 100260;
    input_order.quantity = 25;
    input_order.side = Side::sell;
    input_order.order_type = OrderType::limit;
    input_order.time_in_force = TimeInForce::immediate_or_cancel;
    input_order.order_flags = 0x03;
    input_order.reserved = 0;

    EthernetFrame frame{};
    assert(encode_order_frame(input_envelope, input_order, frame));

    EthernetEnvelope output_envelope{};
    OutboundOrderMessage output_order{};

    const auto result = decode_order_frame(
        std::span<const std::byte>(frame),
        output_envelope,
        output_order);

    assert(result == FrameDecodeError::none);

    assert(output_envelope.destination == input_envelope.destination);
    assert(output_envelope.source == input_envelope.source);

    assert(output_order.sequence_number == input_order.sequence_number);
    assert(output_order.wire_flags == input_order.wire_flags);
    assert(output_order.client_order_id == input_order.client_order_id);
    assert(output_order.instrument_id == input_order.instrument_id);
    assert(output_order.price_ticks == input_order.price_ticks);
    assert(output_order.quantity == input_order.quantity);
    assert(output_order.side == input_order.side);
    assert(output_order.order_type == input_order.order_type);
    assert(output_order.time_in_force == input_order.time_in_force);
    assert(output_order.order_flags == input_order.order_flags);
    assert(output_order.reserved == input_order.reserved);

    MarketDataMessage market_message{};
    market_message.sequence_number = 303;
    market_message.exchange_timestamp_ns = 1234;
    market_message.instrument_id = 7;
    market_message.price_ticks = 100250;
    market_message.quantity = 10;
    market_message.side = Side::buy;
    market_message.action = UpdateAction::add;

    EthernetFrame market_frame{};
    assert(encode_market_data_frame(
        input_envelope,
        market_message,
        market_frame));

    assert(decode_order_frame(
               std::span<const std::byte>(market_frame),
               output_envelope,
               output_order) ==
           FrameDecodeError::wrong_message_type);

    OutboundOrderMessage invalid_order = input_order;
    invalid_order.quantity = 0;

    assert(!encode_order_frame(
        input_envelope,
        invalid_order,
        frame));

    return 0;
}
