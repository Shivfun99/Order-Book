#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/wire_header.hpp"

namespace pulsebook::wire {

struct MarketDataMessage {
    std::uint32_t sequence_number{};
    std::uint32_t wire_flags{};
    std::uint64_t exchange_timestamp_ns{};
    std::uint32_t instrument_id{};
    std::int64_t price_ticks{};
    std::uint32_t quantity{};
    Side side{Side::buy};
    UpdateAction action{UpdateAction::modify};
    std::uint16_t level{};
    std::uint32_t source_flags{};
};

inline bool encode_market_data(const MarketDataMessage& message,
                               WireMessage& output) noexcept {
    if (!is_valid_side(message.side) ||
        !is_valid_update_action(message.action)) {
        return false;
    }

    output.fill(std::byte{0});

    encode_header(output,
                  MessageType::market_data,
                  message.sequence_number,
                  message.wire_flags);

    auto out = std::span<std::byte>(output);
    constexpr std::size_t base = kWireHeaderBytes;

    store_be64(out, base + 0, message.exchange_timestamp_ns);
    store_be32(out, base + 8, message.instrument_id);
    store_i64_be(out, base + 12, message.price_ticks);
    store_be32(out, base + 20, message.quantity);
    out[base + 24] = static_cast<std::byte>(message.side);
    out[base + 25] = static_cast<std::byte>(message.action);
    store_be16(out, base + 26, message.level);
    store_be32(out, base + 28, message.source_flags);

    return true;
}

inline DecodeError decode_market_data(const std::span<const std::byte> input,
                                      MarketDataMessage& output) noexcept {
    WireHeader header{};

    const DecodeError header_error =
        decode_header(input, MessageType::market_data, header);

    if (header_error != DecodeError::none) {
        return header_error;
    }

    constexpr std::size_t base = kWireHeaderBytes;

    const auto side =
        static_cast<Side>(std::to_integer<std::uint8_t>(input[base + 24]));

    const auto action =
        static_cast<UpdateAction>(
            std::to_integer<std::uint8_t>(input[base + 25]));

    if (!is_valid_side(side) ||
        !is_valid_update_action(action)) {
        return DecodeError::invalid_field;
    }

    output.sequence_number = header.sequence_number;
    output.wire_flags = header.flags;
    output.exchange_timestamp_ns = load_be64(input, base + 0);
    output.instrument_id = load_be32(input, base + 8);
    output.price_ticks = load_i64_be(input, base + 12);
    output.quantity = load_be32(input, base + 20);
    output.side = side;
    output.action = action;
    output.level = load_be16(input, base + 26);
    output.source_flags = load_be32(input, base + 28);

    return DecodeError::none;
}

}  // namespace pulsebook::wire
