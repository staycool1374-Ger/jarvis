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

/// @file spinlock.hpp
/// @brief Busy-wait spinlock using atomic exchange.

#pragma once

#include <kernel/arch/io.hpp>
#include <lib/atomic.hpp>

namespace kernel {
namespace sync {

/// @brief Simple busy-wait spinlock using atomic_test_and_set.
class SpinLock {
public:
    SpinLock() = default;

    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&)                 = delete;
    SpinLock& operator=(SpinLock&&)      = delete;

    /// @brief Acquire the lock (spin until available).
    void lock() noexcept {
        while (atomic_exchange(&locked_, 1, __ATOMIC_ACQUIRE)) {
            arch::pause();
        }
    }

    /// @brief Release the lock.
    void unlock() noexcept {
        atomic_store(&locked_, 0, __ATOMIC_RELEASE);
    }

    /// @brief Force-unlock (used to initialise a lock after MemPool allocation).
    void reset() noexcept {
        locked_ = 0;
    }

    /// @brief Attempt to acquire without blocking.
    /// @return true if the lock was acquired.
    bool try_lock() noexcept {
        return !atomic_exchange(&locked_, 1, __ATOMIC_ACQUIRE);
    }

private:
    int locked_ = 0; ///< 0 = unlocked, 1 = locked.
};

} // namespace sync
} // namespace kernel
