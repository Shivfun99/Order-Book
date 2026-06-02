#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/endian.hpp"
#include "pulsebook/wire/wire_constants.hpp"

namespace pulsebook::wire {

enum class DecodeError : std::uint8_t {
    none = 0,
    too_short,
    invalid_magic,
    unsupported_version,
    invalid_header_length,
    invalid_payload_length,
    wrong_message_type,
    invalid_field,
};

struct WireHeader {
    MessageType message_type{MessageType::market_data};
    std::uint32_t sequence_number{};
    std::uint32_t flags{};
};

inline void encode_header(WireMessage& output,
                          const MessageType type,
                          const std::uint32_t sequence_number,
                          const std::uint32_t flags) noexcept {
    auto out = std::span<std::byte>(output);

    store_be16(out, 0, kMagic);
    out[2] = static_cast<std::byte>(kVersion);
    out[3] = static_cast<std::byte>(type);
    store_be16(out, 4, static_cast<std::uint16_t>(kWireHeaderBytes));
    store_be16(out, 6, static_cast<std::uint16_t>(kPayloadBytes));
    store_be32(out, 8, sequence_number);
    store_be32(out, 12, flags);
}

inline DecodeError decode_header(const std::span<const std::byte> input,
                                 const MessageType expected_type,
                                 WireHeader& output) noexcept {
    if (input.size() < kWireMessageBytes) {
        return DecodeError::too_short;
    }

    if (load_be16(input, 0) != kMagic) {
        return DecodeError::invalid_magic;
    }

    if (std::to_integer<std::uint8_t>(input[2]) != kVersion) {
        return DecodeError::unsupported_version;
    }

    if (load_be16(input, 4) != kWireHeaderBytes) {
        return DecodeError::invalid_header_length;
    }

    if (load_be16(input, 6) != kPayloadBytes) {
        return DecodeError::invalid_payload_length;
    }

    const auto decoded_type =
        static_cast<MessageType>(std::to_integer<std::uint8_t>(input[3]));

    if (decoded_type != expected_type) {
        return DecodeError::wrong_message_type;
    }

    output.message_type = decoded_type;
    output.sequence_number = load_be32(input, 8);
    output.flags = load_be32(input, 12);

    return DecodeError::none;
}

}  // namespace pulsebook::wire
