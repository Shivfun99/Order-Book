#include <cassert>
#include <cstddef>
#include <span>

#include "pulsebook/wire/endian.hpp"
#include "pulsebook/wire/market_data_packet.hpp"
#include "pulsebook/wire/order_packet.hpp"

int main() {
    using namespace pulsebook::wire;

    MarketDataMessage market{};
    market.sequence_number = 1;
    market.exchange_timestamp_ns = 10;
    market.instrument_id = 7;
    market.price_ticks = 100;
    market.quantity = 20;
    market.side = Side::buy;
    market.action = UpdateAction::add;

    WireMessage encoded{};
    assert(encode_market_data(market, encoded));

    MarketDataMessage decoded_market{};

    auto wrong_version = encoded;
    wrong_version[2] = std::byte{2};

    assert(decode_market_data(std::span<const std::byte>(wrong_version),
                              decoded_market) ==
           DecodeError::unsupported_version);

    auto wrong_payload_length = encoded;
    store_be16(std::span<std::byte>(wrong_payload_length), 6, 31);

    assert(decode_market_data(std::span<const std::byte>(wrong_payload_length),
                              decoded_market) ==
           DecodeError::invalid_payload_length);

    auto invalid_side = encoded;
    invalid_side[kWireHeaderBytes + 24] = std::byte{9};

    assert(decode_market_data(std::span<const std::byte>(invalid_side),
                              decoded_market) ==
           DecodeError::invalid_field);

    OutboundOrderMessage decoded_order{};

    assert(decode_order(std::span<const std::byte>(encoded), decoded_order) ==
           DecodeError::wrong_message_type);

    return 0;
}
