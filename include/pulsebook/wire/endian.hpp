#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pulsebook::wire {

inline void store_be16(std::span<std::byte> out,
                       const std::size_t offset,
                       const std::uint16_t value) noexcept {
    out[offset] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    out[offset + 1] = static_cast<std::byte>(value & 0xFFU);
}

inline void store_be32(std::span<std::byte> out,
                       const std::size_t offset,
                       const std::uint32_t value) noexcept {
    out[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    out[offset + 1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    out[offset + 2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    out[offset + 3] = static_cast<std::byte>(value & 0xFFU);
}

inline void store_be64(std::span<std::byte> out,
                       const std::size_t offset,
                       const std::uint64_t value) noexcept {
    out[offset] = static_cast<std::byte>((value >> 56U) & 0xFFU);
    out[offset + 1] = static_cast<std::byte>((value >> 48U) & 0xFFU);
    out[offset + 2] = static_cast<std::byte>((value >> 40U) & 0xFFU);
    out[offset + 3] = static_cast<std::byte>((value >> 32U) & 0xFFU);
    out[offset + 4] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    out[offset + 5] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    out[offset + 6] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    out[offset + 7] = static_cast<std::byte>(value & 0xFFU);
}

inline std::uint16_t load_be16(const std::span<const std::byte> in,
                               const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(in[offset])) << 8U) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(in[offset + 1])));
}

inline std::uint32_t load_be32(const std::span<const std::byte> in,
                               const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset])) << 24U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + 1])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + 2])) << 8U) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[offset + 3]));
}

inline std::uint64_t load_be64(const std::span<const std::byte> in,
                               const std::size_t offset) noexcept {
    return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset])) << 56U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 1])) << 48U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 2])) << 40U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 3])) << 32U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 4])) << 24U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 5])) << 16U) |
           (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 6])) << 8U) |
           static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[offset + 7]));
}

inline void store_i64_be(const std::span<std::byte> out,
                         const std::size_t offset,
                         const std::int64_t value) noexcept {
    store_be64(out, offset, std::bit_cast<std::uint64_t>(value));
}

inline std::int64_t load_i64_be(const std::span<const std::byte> in,
                                const std::size_t offset) noexcept {
    return std::bit_cast<std::int64_t>(load_be64(in, offset));
}

}  // namespace pulsebook::wire
