/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel {
namespace sync {

class EventGroup {
public:
    static constexpr size_t MAX_WAITERS = CONFIG_SYNC_MAX_WAITERS;

    EventGroup() : bits_(0), wait_count_(0) {}
    /// @brief Initialize the event group to zero bits.
    void init();

    /// @brief Atomically set bits, waking satisfied waiters.
    void set_bits(uint64_t bits);
    /// @brief Atomically clear bits.
    void clear_bits(uint64_t bits);
    uint64_t get_bits() const { return bits_; }

    /// @brief Block until any of the requested bits are set.
    /// @param bits Bitmask of bits to wait for.
    /// @param clear_on_exit If true, clear matched bits before returning.
    /// @return The bits that were set when the wait completed.
    uint64_t wait_bits(uint64_t bits, bool clear_on_exit = false);
    /// @brief Check if bits are set without blocking.
    /// @return true if any of the requested bits are currently set.
    bool try_wait_bits(uint64_t bits);

private:
    SpinLock lock_;
    uint64_t bits_;
    struct EventWaiter {
        TaskControlBlock* task;
        uint64_t wanted_bits;
        bool clear_on_exit;
    };
    EventWaiter waiters_[MAX_WAITERS];
    size_t wait_count_;

    bool add_waiter(TaskControlBlock& task, uint64_t wanted, bool clear);
    void wake_matching();
};

}
}
