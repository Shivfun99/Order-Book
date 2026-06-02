#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pulsebook::dpdk {

struct LatencySummary {
    std::size_t samples{};
    double mean_ns{};
    double p50_ns{};
    double p95_ns{};
    double p99_ns{};
    double p999_ns{};
    double maximum_ns{};
};

class DpdkLatencyRecorder {
public:
    DpdkLatencyRecorder(const std::size_t capacity,
                        const std::uint64_t tsc_hz)
        : samples_(capacity),
          tsc_hz_(tsc_hz) {
    }

    [[nodiscard]] bool record_cycles(const std::uint64_t cycles) noexcept {
        if (size_ >= samples_.size()) {
            return false;
        }

        samples_[size_] = cycles;
        ++size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] LatencySummary summarize() const {
        LatencySummary summary{};
        summary.samples = size_;

        if (size_ == 0 || tsc_hz_ == 0) {
            return summary;
        }

        std::vector<std::uint64_t> sorted(
            samples_.begin(),
            samples_.begin() + static_cast<std::ptrdiff_t>(size_));

        std::sort(sorted.begin(), sorted.end());

        long double total_cycles = 0.0L;

        for (const std::uint64_t cycles : sorted) {
            total_cycles += static_cast<long double>(cycles);
        }

        summary.mean_ns = cycles_to_ns(
            static_cast<long double>(total_cycles) /
            static_cast<long double>(size_));

        summary.p50_ns = cycles_to_ns(
            static_cast<long double>(percentile(sorted, 0.50L)));

        summary.p95_ns = cycles_to_ns(
            static_cast<long double>(percentile(sorted, 0.95L)));

        summary.p99_ns = cycles_to_ns(
            static_cast<long double>(percentile(sorted, 0.99L)));

        summary.p999_ns = cycles_to_ns(
            static_cast<long double>(percentile(sorted, 0.999L)));

        summary.maximum_ns = cycles_to_ns(
            static_cast<long double>(sorted.back()));

        return summary;
    }

private:
    [[nodiscard]] std::uint64_t percentile(
        const std::vector<std::uint64_t>& sorted,
        const long double quantile) const noexcept {
        const long double rank =
            std::ceil(quantile * static_cast<long double>(sorted.size()));

        const std::size_t index =
            rank <= 1.0L
                ? 0
                : static_cast<std::size_t>(rank - 1.0L);

        return sorted[index < sorted.size() ? index : sorted.size() - 1];
    }

    [[nodiscard]] double cycles_to_ns(
        const long double cycles) const noexcept {
        constexpr long double nanoseconds_per_second = 1'000'000'000.0L;

        return static_cast<double>(
            (cycles * nanoseconds_per_second) /
            static_cast<long double>(tsc_hz_));
    }

    std::vector<std::uint64_t> samples_;
    std::uint64_t tsc_hz_{};
    std::size_t size_{};
};

}  // namespace pulsebook::dpdk
