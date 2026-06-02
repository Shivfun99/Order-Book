#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/market_data_packet.hpp"

int main() {
    using namespace pulsebook::wire;

    static_assert(kWireHeaderBytes == 16);
    static_assert(kPayloadBytes == 32);
    static_assert(kWireMessageBytes == 48);
    static_assert(kEthernetFrameBytesWithoutFcs == 62);

    MarketDataMessage input{};
    input.sequence_number = 7001;
    input.wire_flags = 0xA0B0C0D0U;
    input.exchange_timestamp_ns = 987654321012345ULL;
    input.instrument_id = 42;
    input.price_ticks = 100250;
    input.quantity = 750;
    input.side = Side::buy;
    input.action = UpdateAction::modify;
    input.level = 3;
    input.source_flags = 0x55AA55AAU;

    WireMessage encoded{};
    assert(encode_market_data(input, encoded));

    MarketDataMessage output{};

    const auto error =
        decode_market_data(std::span<const std::byte>(encoded), output);

    assert(error == DecodeError::none);
    assert(output.sequence_number == input.sequence_number);
    assert(output.wire_flags == input.wire_flags);
    assert(output.exchange_timestamp_ns == input.exchange_timestamp_ns);
    assert(output.instrument_id == input.instrument_id);
    assert(output.price_ticks == input.price_ticks);
    assert(output.quantity == input.quantity);
    assert(output.side == input.side);
    assert(output.action == input.action);
    assert(output.level == input.level);
    assert(output.source_flags == input.source_flags);

    auto corrupted = encoded;
    corrupted[0] = std::byte{0};

    MarketDataMessage unused{};

    assert(decode_market_data(std::span<const std::byte>(corrupted), unused) ==
           DecodeError::invalid_magic);

    const auto short_input =
        std::span<const std::byte>(encoded.data(), encoded.size() - 1);

    assert(decode_market_data(short_input, unused) ==
           DecodeError::too_short);

    return 0;
}
