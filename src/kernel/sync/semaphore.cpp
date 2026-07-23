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
    owner_ = nullptr;
    holder_priority_ = 0;
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
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_.reset();
    return errors::SYNC_ERR_OK;
}

/// @brief Add a task to the waiter array (caller must hold lock_).
bool Semaphore::add_waiter(TaskControlBlock *task) {
    // Caller must hold lock_ (wait already holds it)
    if (waiter_count_ >= MAX_WAITERS)
        return false;
    waiters_[waiter_count_++] = task;
    return true;
}

/// @brief Wake the highest-priority waiter (caller must hold lock_).
void Semaphore::wake_one() {
    // Caller must hold lock_ (post already holds it)
    if (waiter_count_ == 0)
        return;

    if (waiter_count_ > 1) {
        size_t best = 0;
        for (size_t i = 1; i < waiter_count_; ++i) {
            if (waiters_[i]->priority > waiters_[best]->priority)
                best = i;
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

/// @brief Boost the owner if the waiter has higher priority (PIP).
void Semaphore::inherit_priority(TaskControlBlock &waiter) {
#if CONFIG_SEMAPHORE_PIP
    if (!owner_)
        return;
    uint64_t max_prio = waiter.priority;
    for (size_t i = 0; i < waiter_count_; ++i) {
        if (waiters_[i]->priority > max_prio)
            max_prio = waiters_[i]->priority;
    }
    if (max_prio > owner_->priority) {
        if (holder_priority_ == 0)
            holder_priority_ = owner_->priority;
        owner_->priority = max_prio;
    }
#else
    (void)waiter;
#endif
}

/// @brief Restore the owner's priority (PIP).
void Semaphore::restore_priority() {
#if CONFIG_SEMAPHORE_PIP
    if (!owner_ || holder_priority_ == 0)
        return;
    uint64_t max_remaining = 0;
    for (size_t i = 0; i < waiter_count_; ++i) {
        if (waiters_[i]->priority > max_remaining)
            max_remaining = waiters_[i]->priority;
    }
    if (max_remaining >= holder_priority_) {
        owner_->priority = max_remaining;
    } else {
        owner_->priority = holder_priority_;
        holder_priority_ = 0;
    }
#endif
}

/// @brief Decrement the count, blocking if zero.
void Semaphore::wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto *task = Scheduler::current_task();
    if (!task)
        return;

    if (count_ > 0) {
        --count_;
        if (count_ == 0) {
            owner_ = task;
        }
        return;
    }

    inherit_priority(*task);

    bool added = add_waiter(task);
    ENSURE(added);

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

/// @brief Decrement the count, blocking if zero (error-returning overload).
errors::SyncError Semaphore::wait_err() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto *task = Scheduler::current_task();
    if (!task)
        return errors::SYNC_ERR_NO_TASK;

    if (count_ > 0) {
        --count_;
        if (count_ == 0) {
            owner_ = task;
        }
        return errors::SYNC_ERR_OK;
    }

    if (waiter_count_ >= MAX_WAITERS) {
        return errors::SYNC_ERR_MAX_WAITERS;
    }

    inherit_priority(*task);

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
        restore_priority();
    } else if (count_ < max_count_) {
        if (count_ == 0)
            owner_ = nullptr;
        ++count_;
    }
}

/// @brief Increment count, waking a waiter if any (error-returning overload).
errors::SyncError Semaphore::post_err() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ > 0) {
        wake_one();
        restore_priority();
        return errors::SYNC_ERR_OK;
    }
    if (count_ < max_count_) {
        if (count_ == 0)
            owner_ = nullptr;
        ++count_;
        return errors::SYNC_ERR_OK;
    }
    return errors::SYNC_ERR_BUFFER_FULL;
}

} // namespace sync
} // namespace kernel
