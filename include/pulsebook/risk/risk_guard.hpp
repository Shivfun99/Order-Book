#pragma once

#include <cstdint>

#include "pulsebook/order/order_request.hpp"

namespace pulsebook {

struct RiskLimits {
    Quantity max_order_quantity{100};
    std::int64_t max_absolute_position{10000};
    std::uint64_t max_notional_ticks{5'000'000'000ULL};
    std::uint32_t max_outstanding_orders{1024};
};

class RiskGuard {
public:
    explicit RiskGuard(const RiskLimits limits = {}) noexcept
        : limits_(limits) {
    }

    [[nodiscard]] bool validate_and_commit(
        const OrderRequest& order) noexcept {
        if (kill_switch_ ||
            order.quantity == 0 ||
            order.quantity > limits_.max_order_quantity ||
            order.price_ticks <= 0 ||
            outstanding_orders_ >= limits_.max_outstanding_orders) {
            return false;
        }

        const std::int64_t signed_quantity =
            order.side == Side::buy
                ? static_cast<std::int64_t>(order.quantity)
                : -static_cast<std::int64_t>(order.quantity);

        const std::int64_t proposed_position =
            position_ + signed_quantity;

        const std::uint64_t absolute_position =
            absolute_value(proposed_position);

        if (absolute_position >
            static_cast<std::uint64_t>(limits_.max_absolute_position)) {
            return false;
        }

        const std::uint64_t absolute_price =
            absolute_value(order.price_ticks);

        if (absolute_price != 0 &&
            absolute_position >
                limits_.max_notional_ticks / absolute_price) {
            return false;
        }

        position_ = proposed_position;
        ++outstanding_orders_;
        return true;
    }

    void set_kill_switch(const bool enabled) noexcept {
        kill_switch_ = enabled;
    }

    void acknowledge_one() noexcept {
        if (outstanding_orders_ > 0) {
            --outstanding_orders_;
        }
    }

    [[nodiscard]] std::int64_t position() const noexcept {
        return position_;
    }

    [[nodiscard]] std::uint32_t outstanding_orders() const noexcept {
        return outstanding_orders_;
    }

private:
    [[nodiscard]] static std::uint64_t absolute_value(
        const std::int64_t value) noexcept {
        if (value >= 0) {
            return static_cast<std::uint64_t>(value);
        }

        return static_cast<std::uint64_t>(-(value + 1)) + 1U;
    }

    RiskLimits limits_{};
    std::int64_t position_{};
    std::uint32_t outstanding_orders_{};
    bool kill_switch_{};
};

}  // namespace pulsebook
