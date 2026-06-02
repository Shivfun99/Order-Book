#pragma once

#include <cstdint>

#include "pulsebook/book/fixed_l2_book.hpp"

namespace pulsebook {

struct StrategyConfig {
    std::int64_t imbalance_threshold_bps{6000};
    Quantity order_quantity{10};
};

struct StrategyDecision {
    Signal signal{Signal::no_signal};
    PriceTicks limit_price_ticks{};
    Quantity quantity{};
    std::int64_t imbalance_bps{};
};

class ImbalanceStrategy {
public:
    explicit ImbalanceStrategy(const StrategyConfig config = {}) noexcept
        : config_(config) {
    }

    [[nodiscard]] StrategyDecision evaluate(
        const FixedL2Book& book) const noexcept {
        StrategyDecision decision{};

        if (!book.has_complete_visible_depth()) {
            return decision;
        }

        const std::uint64_t bid_quantity = book.total_bid_quantity();
        const std::uint64_t ask_quantity = book.total_ask_quantity();
        const std::uint64_t total_quantity = bid_quantity + ask_quantity;

        if (total_quantity == 0) {
            return decision;
        }

        const auto signed_bid_quantity =
            static_cast<std::int64_t>(bid_quantity);
        const auto signed_ask_quantity =
            static_cast<std::int64_t>(ask_quantity);
        const auto signed_total_quantity =
            static_cast<std::int64_t>(total_quantity);

        decision.imbalance_bps =
            ((signed_bid_quantity - signed_ask_quantity) * 10000) /
            signed_total_quantity;

        if (decision.imbalance_bps >= config_.imbalance_threshold_bps) {
            const auto best_ask = book.best_ask();

            if (best_ask.has_value()) {
                decision.signal = Signal::buy;
                decision.limit_price_ticks = *best_ask;
                decision.quantity = config_.order_quantity;
            }

            return decision;
        }

        if (decision.imbalance_bps <= -config_.imbalance_threshold_bps) {
            const auto best_bid = book.best_bid();

            if (best_bid.has_value()) {
                decision.signal = Signal::sell;
                decision.limit_price_ticks = *best_bid;
                decision.quantity = config_.order_quantity;
            }
        }

        return decision;
    }

private:
    StrategyConfig config_{};
};

}  // namespace pulsebook
