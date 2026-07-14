#pragma once

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

/// @file mutex.hpp
/// @brief Mutex with priority inheritance and blocking waiters.

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/sync/sync_errors.hpp>

namespace kernel {
namespace sync {

class Mutex {
  public:
    static constexpr size_t MAX_WAITERS = CONFIG_SYNC_MAX_WAITERS;

    Mutex()
        : owner_(nullptr), holder_priority_(0), lock_count_(0), wait_count_(0) {
    }
    /// @brief Initialize the mutex to unlocked state.
    void init();
    /// @brief Initialize the mutex to unlocked state (error-returning
    /// overload).
    /// @return SYNC_ERR_OK on success, SYNC_ERR_ALREADY_INITIALIZED if already
    /// initialized.
    errors::SyncError init_err();

    /// @brief Acquire the mutex, blocking until available.
    void lock();
    /// @brief Acquire the mutex, blocking until available (error-returning
    /// overload).
    /// @return SYNC_ERR_OK on success, SYNC_ERR_NO_TASK if no current task,
    /// SYNC_ERR_MAX_WAITERS if waiter limit reached.
    errors::SyncError lock_err();

    /// @brief Attempt to acquire the mutex without blocking.
    /// @return true if the lock was acquired.
    bool try_lock();
    /// @brief Attempt to acquire the mutex without blocking (error-returning
    /// overload).
    /// @return SYNC_ERR_OK on success, SYNC_ERR_NO_TASK if no current task.
    errors::SyncError try_lock_err();

    /// @brief Release the mutex, waking the next waiter if any.
    void unlock();
    /// @brief Release the mutex, waking the next waiter if any (error-returning
    /// overload).
    /// @return SYNC_ERR_OK on success, SYNC_ERR_NOT_OWNER if not the owner,
    /// SYNC_ERR_NOT_LOCKED if not locked.
    errors::SyncError unlock_err();

    bool is_locked() const {
        return owner_ != nullptr;
    }
    TaskControlBlock *owner() const {
        return owner_;
    }

  private:
    SpinLock lock_;            ///< Protects all mutex state.
    TaskControlBlock *owner_;  ///< Current lock holder (nullptr = unlocked).
    uint64_t holder_priority_; ///< Saved original priority of owner (for PI
                               ///< restore).
    uint64_t lock_count_;      ///< Recursive lock count.
    TaskControlBlock *waiters_[MAX_WAITERS]; ///< Array of waiting tasks
                                             ///< (priority-sorted on wake).
    size_t wait_count_;                      ///< Number of waiting tasks.

    /// @brief Add a task to the waiter array.
    bool add_waiter(TaskControlBlock &task);
    /// @brief Wake the highest-priority waiter.
    void wake_one();
    /// @brief Boost owner priority if waiter is higher priority.
    void inherit_priority(TaskControlBlock &waiter);
    /// @brief Restore owner priority to its original value.
    void restore_priority();
    /// @brief Re-evaluate owner priority after a waiter's priority changed.
    ///        Used for transitive PI chain propagation.
    void reevaluate();
};

} // namespace sync
} // namespace kernel
