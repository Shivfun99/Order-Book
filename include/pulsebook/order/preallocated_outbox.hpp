#pragma once

#include <array>
#include <cstddef>

#include "pulsebook/order/order_request.hpp"

namespace pulsebook {

template <std::size_t Capacity>
class PreallocatedOutbox {
public:
    static_assert(Capacity > 0);

    [[nodiscard]] bool push(const OrderRequest& order) noexcept {
        if (size_ == Capacity) {
            return false;
        }

        entries_[tail_] = order;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    [[nodiscard]] bool pop(OrderRequest& order) noexcept {
        if (size_ == 0) {
            return false;
        }

        order = entries_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        return size_ == Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    std::array<OrderRequest, Capacity> entries_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
};

}  // namespace pulsebook
