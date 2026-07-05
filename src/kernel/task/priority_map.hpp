#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>
#include <kernel/arch/hal/bits.hpp>

namespace kernel {

class PriorityMap {
    static_assert(CONFIG_PRIORITY_CEILING <= 128,
                  "O(1) scheduler supports at most 128 priority levels");

    uint64_t bitmap_hi_;  ///< Priorities 64–127
    uint64_t bitmap_lo_;  ///< Priorities 0–63

public:
    PriorityMap() : bitmap_hi_(0), bitmap_lo_(0) {}

    void set(uint64_t prio) noexcept {
        if (prio >= 64) {
            bitmap_hi_ |= (1ULL << (prio - 64));
        } else {
            bitmap_lo_ |= (1ULL << prio);
        }
    }

    void clear(uint64_t prio) noexcept {
        if (prio >= 64) {
            bitmap_hi_ &= ~(1ULL << (prio - 64));
        } else {
            bitmap_lo_ &= ~(1ULL << prio);
        }
    }

    uint64_t get_highest_priority() const noexcept {
        if (bitmap_hi_) {
            return 64 + hal::bits::find_highest_bit(bitmap_hi_);
        }
        if (bitmap_lo_) {
            return hal::bits::find_highest_bit(bitmap_lo_);
        }
        return 0;
    }

    bool is_set(uint64_t prio) const noexcept {
        if (prio >= 64) {
            return (bitmap_hi_ & (1ULL << (prio - 64))) != 0;
        }
        return (bitmap_lo_ & (1ULL << prio)) != 0;
    }

    uint64_t raw_hi() const noexcept { return bitmap_hi_; }
    uint64_t raw_lo() const noexcept { return bitmap_lo_; }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void set_raw(uint64_t hi, uint64_t lo) noexcept { bitmap_hi_ = hi; bitmap_lo_ = lo; }

    bool empty() const noexcept { return bitmap_hi_ == 0 && bitmap_lo_ == 0; }

    void clear_all() noexcept { bitmap_hi_ = 0; bitmap_lo_ = 0; }
};

} // namespace kernel
