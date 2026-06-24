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

class Semaphore {
public:
    static constexpr size_t MAX_WAITERS = CONFIG_SYNC_MAX_WAITERS;

    Semaphore()
        : count_(0)
        , max_count_(1)
        , waiter_count_(0)
        {}
    /// @brief Initialize semaphore with a count and optional maximum.
    /// @param initial Starting count.
    /// @param max Maximum count (default unlimited).
    void init(uint64_t initial, uint64_t max = 0xFFFFFFFF);

    /// @brief Decrement count, blocking if zero.
    void wait();
    /// @brief Decrement count without blocking.
    /// @return true if the count was decremented.
    bool try_wait();
    /// @brief Increment count, waking a waiter if any.
    void post();

    uint64_t value() const { return count_; }

private:
    SpinLock lock_;
    uint64_t count_;
    uint64_t max_count_;
    TaskControlBlock* waiters_[MAX_WAITERS];
    size_t waiter_count_;

    bool add_waiter(TaskControlBlock* task);
    void wake_one();
};

}
}
