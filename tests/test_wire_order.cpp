#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/order_packet.hpp"

int main() {
    using namespace pulsebook::wire;

    OutboundOrderMessage input{};
    input.sequence_number = 8002;
    input.wire_flags = 0x11000022U;
    input.client_order_id = 9000000001ULL;
    input.instrument_id = 42;
    input.price_ticks = 100260;
    input.quantity = 25;
    input.side = Side::sell;
    input.order_type = OrderType::limit;
    input.time_in_force = TimeInForce::immediate_or_cancel;
    input.order_flags = 0x03;
    input.reserved = 0;

    WireMessage encoded{};
    assert(encode_order(input, encoded));

    OutboundOrderMessage output{};

    const auto error =
        decode_order(std::span<const std::byte>(encoded), output);

    assert(error == DecodeError::none);
    assert(output.sequence_number == input.sequence_number);
    assert(output.wire_flags == input.wire_flags);
    assert(output.client_order_id == input.client_order_id);
    assert(output.instrument_id == input.instrument_id);
    assert(output.price_ticks == input.price_ticks);
    assert(output.quantity == input.quantity);
    assert(output.side == input.side);
    assert(output.order_type == input.order_type);
    assert(output.time_in_force == input.time_in_force);
    assert(output.order_flags == input.order_flags);
    assert(output.reserved == input.reserved);

    OutboundOrderMessage invalid = input;
    invalid.quantity = 0;

    assert(!encode_order(invalid, encoded));

    return 0;
}
