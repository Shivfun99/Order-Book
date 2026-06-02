#pragma once

#include <cstddef>
#include <cstdint>

#include "pulsebook/book/fixed_l2_book.hpp"
#include "pulsebook/order/order_request.hpp"
#include "pulsebook/order/preallocated_outbox.hpp"
#include "pulsebook/risk/risk_guard.hpp"
#include "pulsebook/strategy/imbalance_strategy.hpp"

namespace pulsebook {

enum class EngineStatus : std::uint8_t {
    no_order = 0,
    order_emitted,
    invalid_update,
    risk_rejected,
    outbox_full,
};

struct EngineConfig {
    InstrumentId instrument_id{77};
    StrategyConfig strategy{};
    RiskLimits risk{};
};

struct EngineResult {
    EngineStatus status{EngineStatus::no_order};
    Signal signal{Signal::no_signal};
    std::int64_t imbalance_bps{};
};

template <std::size_t OutboxCapacity = 64>
class TradingEngine {
public:
    explicit TradingEngine(const EngineConfig config = {}) noexcept
        : config_(config),
          strategy_(config.strategy),
          risk_(config.risk) {
    }

    [[nodiscard]] EngineResult on_market_update(
        const MarketUpdate& update) noexcept {
        if (update.instrument_id != config_.instrument_id ||
            !book_.apply(update)) {
            return {
                EngineStatus::invalid_update,
                Signal::no_signal,
                0,
            };
        }

        const StrategyDecision decision = strategy_.evaluate(book_);

        if (decision.signal == Signal::no_signal) {
            return {
                EngineStatus::no_order,
                Signal::no_signal,
                decision.imbalance_bps,
            };
        }

        if (outbox_.full()) {
            return {
                EngineStatus::outbox_full,
                decision.signal,
                decision.imbalance_bps,
            };
        }

        OrderRequest order{};
        order.client_order_id = next_client_order_id_;
        order.price_ticks = decision.limit_price_ticks;
        order.quantity = decision.quantity;
        order.instrument_id = update.instrument_id;
        order.source_sequence = update.sequence_number;
        order.side =
            decision.signal == Signal::buy ? Side::buy : Side::sell;
        order.order_kind = OrderKind::limit;
        order.flags = 0;

        if (!risk_.validate_and_commit(order)) {
            return {
                EngineStatus::risk_rejected,
                decision.signal,
                decision.imbalance_bps,
            };
        }

        if (!outbox_.push(order)) {
            return {
                EngineStatus::outbox_full,
                decision.signal,
                decision.imbalance_bps,
            };
        }

        ++next_client_order_id_;

        return {
            EngineStatus::order_emitted,
            decision.signal,
            decision.imbalance_bps,
        };
    }

    [[nodiscard]] bool pop_order(OrderRequest& order) noexcept {
        return outbox_.pop(order);
    }

    [[nodiscard]] const FixedL2Book& book() const noexcept {
        return book_;
    }

    void acknowledge_one_order() noexcept {
        risk_.acknowledge_one();
    }

    [[nodiscard]] const RiskGuard& risk() const noexcept {
        return risk_;
    }

private:
    EngineConfig config_{};
    FixedL2Book book_{};
    ImbalanceStrategy strategy_{};
    RiskGuard risk_{};
    PreallocatedOutbox<OutboxCapacity> outbox_{};
    std::uint64_t next_client_order_id_{1};
};

}  // namespace pulsebook
