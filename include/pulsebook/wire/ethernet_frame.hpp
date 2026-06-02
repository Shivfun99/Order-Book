#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pulsebook/wire/endian.hpp"
#include "pulsebook/wire/market_data_packet.hpp"
#include "pulsebook/wire/order_packet.hpp"
#include "pulsebook/wire/wire_constants.hpp"

namespace pulsebook::wire {

inline constexpr std::size_t kMacAddressBytes = 6;
inline constexpr std::size_t kDestinationMacOffset = 0;
inline constexpr std::size_t kSourceMacOffset = 6;
inline constexpr std::size_t kEtherTypeOffset = 12;
inline constexpr std::size_t kWirePayloadOffset = kEthernetHeaderBytes;

using MacAddress = std::array<std::byte, kMacAddressBytes>;
using EthernetFrame = std::array<std::byte, kEthernetFrameBytesWithoutFcs>;

struct EthernetEnvelope {
    MacAddress destination{};
    MacAddress source{};
};

enum class FrameDecodeError : std::uint8_t {
    none = 0,
    too_short,
    wrong_ether_type,
    invalid_magic,
    unsupported_version,
    invalid_header_length,
    invalid_payload_length,
    wrong_message_type,
    invalid_field,
};

constexpr FrameDecodeError to_frame_decode_error(
    const DecodeError error) noexcept {
    switch (error) {
        case DecodeError::none:
            return FrameDecodeError::none;
        case DecodeError::too_short:
            return FrameDecodeError::too_short;
        case DecodeError::invalid_magic:
            return FrameDecodeError::invalid_magic;
        case DecodeError::unsupported_version:
            return FrameDecodeError::unsupported_version;
        case DecodeError::invalid_header_length:
            return FrameDecodeError::invalid_header_length;
        case DecodeError::invalid_payload_length:
            return FrameDecodeError::invalid_payload_length;
        case DecodeError::wrong_message_type:
            return FrameDecodeError::wrong_message_type;
        case DecodeError::invalid_field:
            return FrameDecodeError::invalid_field;
    }

    return FrameDecodeError::invalid_field;
}

inline void write_mac_address(std::span<std::byte> output,
                              const std::size_t offset,
                              const MacAddress& address) noexcept {
    std::copy(address.begin(), address.end(), output.begin() + offset);
}

inline MacAddress read_mac_address(const std::span<const std::byte> input,
                                   const std::size_t offset) noexcept {
    MacAddress address{};
    std::copy_n(input.begin() + offset, kMacAddressBytes, address.begin());
    return address;
}

inline void encode_ethernet_header(const EthernetEnvelope& envelope,
                                   EthernetFrame& output) noexcept {
    auto frame = std::span<std::byte>(output);

    write_mac_address(frame, kDestinationMacOffset, envelope.destination);
    write_mac_address(frame, kSourceMacOffset, envelope.source);
    store_be16(frame, kEtherTypeOffset, kEtherType);
}

inline FrameDecodeError decode_ethernet_header(
    const std::span<const std::byte> input,
    EthernetEnvelope& output) noexcept {
    if (input.size() < kEthernetFrameBytesWithoutFcs) {
        return FrameDecodeError::too_short;
    }

    if (load_be16(input, kEtherTypeOffset) != kEtherType) {
        return FrameDecodeError::wrong_ether_type;
    }

    output.destination = read_mac_address(input, kDestinationMacOffset);
    output.source = read_mac_address(input, kSourceMacOffset);

    return FrameDecodeError::none;
}

inline bool encode_market_data_frame(const EthernetEnvelope& envelope,
                                     const MarketDataMessage& message,
                                     EthernetFrame& output) noexcept {
    WireMessage wire_message{};

    if (!encode_market_data(message, wire_message)) {
        return false;
    }

    output.fill(std::byte{0});
    encode_ethernet_header(envelope, output);

    std::copy(wire_message.begin(),
              wire_message.end(),
              output.begin() + kWirePayloadOffset);

    return true;
}

inline FrameDecodeError decode_market_data_frame(
    const std::span<const std::byte> input,
    EthernetEnvelope& envelope,
    MarketDataMessage& output) noexcept {
    const auto ethernet_error = decode_ethernet_header(input, envelope);

    if (ethernet_error != FrameDecodeError::none) {
        return ethernet_error;
    }

    const auto payload =
        input.subspan(kWirePayloadOffset, kWireMessageBytes);

    return to_frame_decode_error(decode_market_data(payload, output));
}

inline bool encode_order_frame(const EthernetEnvelope& envelope,
                               const OutboundOrderMessage& order,
                               EthernetFrame& output) noexcept {
    WireMessage wire_message{};

    if (!encode_order(order, wire_message)) {
        return false;
    }

    output.fill(std::byte{0});
    encode_ethernet_header(envelope, output);

    std::copy(wire_message.begin(),
              wire_message.end(),
              output.begin() + kWirePayloadOffset);

    return true;
}

inline FrameDecodeError decode_order_frame(
    const std::span<const std::byte> input,
    EthernetEnvelope& envelope,
    OutboundOrderMessage& output) noexcept {
    const auto ethernet_error = decode_ethernet_header(input, envelope);

    if (ethernet_error != FrameDecodeError::none) {
        return ethernet_error;
    }

    const auto payload =
        input.subspan(kWirePayloadOffset, kWireMessageBytes);

    return to_frame_decode_error(decode_order(payload, output));
}

}  // namespace pulsebook::wire
