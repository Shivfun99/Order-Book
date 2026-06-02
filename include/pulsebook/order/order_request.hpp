#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "pulsebook/common/types.hpp"

namespace pulsebook {

enum class OrderKind : std::uint8_t {
    limit = 1,
};

struct alignas(8) OrderRequest {
    std::uint64_t client_order_id{};
    PriceTicks price_ticks{};
    Quantity quantity{};
    InstrumentId instrument_id{};
    SequenceNumber source_sequence{};
    Side side{Side::buy};
    OrderKind order_kind{OrderKind::limit};
    std::uint16_t flags{};
};

static_assert(sizeof(OrderRequest) == 32);
static_assert(std::is_trivially_copyable_v<OrderRequest>);

}  // namespace pulsebook
