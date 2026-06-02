#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "pulsebook/common/types.hpp"

namespace pulsebook {

inline constexpr std::size_t kVisibleDepth = 5;

struct BookLevel {
    PriceTicks price_ticks{};
    Quantity quantity{};
    bool active{};
};

class FixedL2Book {
public:
    [[nodiscard]] bool apply(const MarketUpdate& update) noexcept {
        if (!is_valid_side(update.side) ||
            !is_valid_action(update.action) ||
            update.level >= kVisibleDepth) {
            return false;
        }

        auto& levels = update.side == Side::buy ? bids_ : asks_;
        BookLevel& level = levels[update.level];

        if (update.action == UpdateAction::erase || update.quantity == 0) {
            level = {};
            return true;
        }

        if (update.price_ticks <= 0) {
            return false;
        }

        level.price_ticks = update.price_ticks;
        level.quantity = update.quantity;
        level.active = true;

        return true;
    }

    [[nodiscard]] bool has_complete_visible_depth() const noexcept {
        for (std::size_t index = 0; index < kVisibleDepth; ++index) {
            if (!bids_[index].active || !asks_[index].active) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::uint64_t total_bid_quantity() const noexcept {
        std::uint64_t total{};

        for (const auto& level : bids_) {
            if (level.active) {
                total += level.quantity;
            }
        }

        return total;
    }

    [[nodiscard]] std::uint64_t total_ask_quantity() const noexcept {
        std::uint64_t total{};

        for (const auto& level : asks_) {
            if (level.active) {
                total += level.quantity;
            }
        }

        return total;
    }

    [[nodiscard]] std::optional<PriceTicks> best_bid() const noexcept {
        std::optional<PriceTicks> result{};

        for (const auto& level : bids_) {
            if (!level.active) {
                continue;
            }

            if (!result.has_value() || level.price_ticks > *result) {
                result = level.price_ticks;
            }
        }

        return result;
    }

    [[nodiscard]] std::optional<PriceTicks> best_ask() const noexcept {
        std::optional<PriceTicks> result{};

        for (const auto& level : asks_) {
            if (!level.active) {
                continue;
            }

            if (!result.has_value() || level.price_ticks < *result) {
                result = level.price_ticks;
            }
        }

        return result;
    }

private:
    std::array<BookLevel, kVisibleDepth> bids_{};
    std::array<BookLevel, kVisibleDepth> asks_{};
};

}  // namespace pulsebook
