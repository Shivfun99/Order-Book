#pragma once

#include <cstdint>

namespace pulsebook {

using PriceTicks = std::int64_t;
using Quantity = std::uint32_t;
using InstrumentId = std::uint32_t;
using SequenceNumber = std::uint32_t;

enum class Side : std::uint8_t {
    buy = 1,
    sell = 2,
};

enum class UpdateAction : std::uint8_t {
    add = 1,
    modify = 2,
    erase = 3,
};

enum class Signal : std::uint8_t {
    no_signal = 0,
    buy = 1,
    sell = 2,
};

struct MarketUpdate {
    SequenceNumber sequence_number{};
    std::uint64_t exchange_timestamp_ns{};
    InstrumentId instrument_id{};
    PriceTicks price_ticks{};
    Quantity quantity{};
    Side side{Side::buy};
    UpdateAction action{UpdateAction::modify};
    std::uint16_t level{};
    std::uint32_t source_flags{};
};

constexpr bool is_valid_side(const Side side) noexcept {
    return side == Side::buy || side == Side::sell;
}

constexpr bool is_valid_action(const UpdateAction action) noexcept {
    return action == UpdateAction::add ||
           action == UpdateAction::modify ||
           action == UpdateAction::erase;
}

}  // namespace pulsebook
