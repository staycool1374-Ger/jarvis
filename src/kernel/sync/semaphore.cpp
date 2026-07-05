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

/// @file semaphore.cpp
/// @brief Counting semaphore implementation — init, wait, post, try_wait.

#include <kernel/sync/semaphore.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <assert.hpp>

namespace kernel {
namespace sync {

/// @brief Initialise the semaphore with a starting count and optional max.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Semaphore::init(uint64_t initial, uint64_t max) {
    count_ = initial;
    max_count_ = max;
    waiter_count_ = 0;
    lock_.reset();
}

/// @brief Initialise the semaphore (error-returning overload).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
errors::SyncError Semaphore::init_err(uint64_t initial, uint64_t max) {
    if (waiter_count_ != 0 || count_ != 0 || max_count_ != 1) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    count_ = initial;
    max_count_ = max;
    waiter_count_ = 0;
    lock_.reset();
    return errors::SYNC_ERR_OK;
}

/// @brief Add a task to the waiter array (caller must hold lock_).
bool Semaphore::add_waiter(TaskControlBlock* task) {
    // Caller must hold lock_ (wait already holds it)
    if (waiter_count_ >= MAX_WAITERS) return false;
    waiters_[waiter_count_++] = task;
    return true;
}

/// @brief Wake the highest-priority waiter (caller must hold lock_).
void Semaphore::wake_one() {
    // Caller must hold lock_ (post already holds it)
    if (waiter_count_ == 0) return;

    if (waiter_count_ > 1) {
        size_t best = 0;
        for (size_t i = 1; i < waiter_count_; ++i) {
            if (waiters_[i]->priority > waiters_[best]->priority) best = i;
        }
        if (waiters_[best]->state != TaskState::TERMINATED)
            Scheduler::set_task_ready(*waiters_[best]);
        waiters_[best] = waiters_[--waiter_count_];
    } else {
        if (waiters_[0]->state != TaskState::TERMINATED)
            Scheduler::set_task_ready(*waiters_[0]);
        waiter_count_ = 0;
    }
}

/// @brief Decrement the count, blocking if zero.
void Semaphore::wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return;

    if (count_ > 0) {
        --count_;
        return;
    }

    bool added = add_waiter(task);
    ENSURE(added);

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

/// @brief Decrement the count, blocking if zero (error-returning overload).
errors::SyncError Semaphore::wait_err() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return errors::SYNC_ERR_NO_TASK;

    if (count_ > 0) {
        --count_;
        return errors::SYNC_ERR_OK;
    }

    if (waiter_count_ >= MAX_WAITERS) {
        return errors::SYNC_ERR_MAX_WAITERS;
    }

    waiters_[waiter_count_++] = task;
    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    return errors::SYNC_ERR_OK;
}

/// @brief Decrement count without blocking.
bool Semaphore::try_wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (count_ > 0) {
        --count_;
        return true;
    }
    return false;
}

/// @brief Decrement count without blocking (error-returning overload).
errors::SyncError Semaphore::try_wait_err() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (count_ > 0) {
        --count_;
        return errors::SYNC_ERR_OK;
    }
    return errors::SYNC_ERR_QUEUE_EMPTY;
}

/// @brief Increment count, waking a waiter if any.
void Semaphore::post() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ > 0) {
        wake_one();
    } else if (count_ < max_count_) {
        ++count_;
    }
}

/// @brief Increment count, waking a waiter if any (error-returning overload).
errors::SyncError Semaphore::post_err() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ > 0) {
        wake_one();
        return errors::SYNC_ERR_OK;
    }
    if (count_ < max_count_) {
        ++count_;
        return errors::SYNC_ERR_OK;
    }
    return errors::SYNC_ERR_BUFFER_FULL;
}

}
}
