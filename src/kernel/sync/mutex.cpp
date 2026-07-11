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

/// @file mutex.cpp
/// @brief Mutex implementation — lock, unlock, try_lock, priority inheritance.

#include <kernel/sync/mutex.hpp>
#include <kernel/task/scheduler.hpp>
#include <assert.hpp>

namespace kernel {
namespace sync {

/// @brief Initialise the mutex to unlocked state.
void Mutex::init() {
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
}

/// @brief Initialise the mutex (error-returning overload).
errors::SyncError Mutex::init_err() {
    if (owner_ != nullptr || lock_count_ != 0 || wait_count_ != 0) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
    return errors::SYNC_ERR_OK;
}

/// @brief Add a task to the waiter array.
/// @return false if the array is full.
bool Mutex::add_waiter(TaskControlBlock& task) {
    for (size_t i = 0; i < wait_count_; ++i) {
        ENSURE(waiters_[i] != &task);
    }
    if (wait_count_ >= MAX_WAITERS) return false;
    waiters_[wait_count_++] = &task;
    return true;
}

/// @brief Wake the highest-priority waiter.
void Mutex::wake_one() {
    if (wait_count_ == 0) return;

    size_t best = 0;
    for (size_t i = 1; i < wait_count_; ++i) {
        if (waiters_[i] && (!waiters_[best] ||
            waiters_[i]->priority > waiters_[best]->priority)) best = i;
    }

    if (waiters_[best] && waiters_[best]->state != TaskState::TERMINATED) {
        waiters_[best]->waiting_on_mutex = nullptr;
        Scheduler::set_task_ready(*waiters_[best]);
    }
    waiters_[best] = waiters_[--wait_count_];
}

/// @brief Boost the owner's priority if the waiter has higher priority.
///        Scans all waiters to find the max priority (handles multiple
///        waiters at different priorities and transitive chains).
void Mutex::inherit_priority(TaskControlBlock& waiter) {
    if (!owner_) return;

    uint64_t max_prio = waiter.priority;
    for (size_t i = 0; i < wait_count_; ++i) {
        if (waiters_[i]->priority > max_prio) max_prio = waiters_[i]->priority;
    }

    if (max_prio > owner_->priority) {
        if (holder_priority_ == 0)
            holder_priority_ = owner_->priority;
        owner_->priority = max_prio;

        // Transitive PI: if the owner is itself blocked on another mutex,
        // propagate the boost up the chain.
        if (owner_->waiting_on_mutex)
            owner_->waiting_on_mutex->reevaluate();
    }
}

/// @brief Restore the owner's priority to its saved original value,
///        unless remaining waiters still need a boost.
void Mutex::restore_priority() {
    if (!owner_ || holder_priority_ == 0) return;

    uint64_t max_remaining = 0;
    for (size_t i = 0; i < wait_count_; ++i) {
        if (waiters_[i]->priority > max_remaining)
            max_remaining = waiters_[i]->priority;
    }

    if (max_remaining > holder_priority_) {
        owner_->priority = max_remaining;
    } else {
        owner_->priority = holder_priority_;
        holder_priority_ = 0;
    }
}

/// @brief Re-evaluate the owner's priority based on all current waiters.
///        Called when a waiter's priority changes (e.g. transitively boosted
///        by another mutex further down the chain).
void Mutex::reevaluate() {
    if (!owner_ || wait_count_ == 0) return;

    uint64_t max_prio = 0;
    for (size_t i = 0; i < wait_count_; ++i) {
        if (waiters_[i]->priority > max_prio) max_prio = waiters_[i]->priority;
    }

    if (max_prio > owner_->priority) {
        if (holder_priority_ == 0)
            holder_priority_ = owner_->priority;
        owner_->priority = max_prio;

        if (owner_->waiting_on_mutex)
            owner_->waiting_on_mutex->reevaluate();
    }
}

/// @brief Acquire the mutex, blocking until available (supports recursion).
void Mutex::lock() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task) { lock_.unlock(); return; }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        lock_.unlock();
        return;
    }

    inherit_priority(*task);

    bool added = add_waiter(*task);
    ENSURE(added);
    task->waiting_on_mutex = this;

    lock_.unlock();

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

/// @brief Acquire the mutex (error-returning overload).
errors::SyncError Mutex::lock_err() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task) { lock_.unlock(); return errors::SYNC_ERR_NO_TASK; }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    inherit_priority(*task);

    bool added = add_waiter(*task);
    if (!added) {
        lock_.unlock();
        return errors::SYNC_ERR_MAX_WAITERS;
    }
    task->waiting_on_mutex = this;

    lock_.unlock();

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
    return errors::SYNC_ERR_OK;
}

/// @brief Attempt to acquire without blocking.
bool Mutex::try_lock() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task) { lock_.unlock(); return false; }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return true;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        lock_.unlock();
        return true;
    }

    lock_.unlock();
    return false;
}

/// @brief Attempt to acquire without blocking (error-returning overload).
errors::SyncError Mutex::try_lock_err() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task) { lock_.unlock(); return errors::SYNC_ERR_NO_TASK; }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    lock_.unlock();
    return errors::SYNC_ERR_NOT_OWNER;
}

/// @brief Release the mutex, waking the next highest-priority waiter.
void Mutex::unlock() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task || owner_ != task) { lock_.unlock(); return; }

    if (lock_count_ > 1) {
        --lock_count_;
        lock_.unlock();
        return;
    }

    // Wake the highest-priority waiter BEFORE restoring the old
    // owner's priority, so that the awoken waiter is no longer
    // counted in the max-remaining-waiter calculation.
    if (wait_count_ > 0) {
        wake_one();
    }

    restore_priority();

    owner_ = nullptr;
    lock_count_ = 0;

    lock_.unlock();
}

/// @brief Release the mutex (error-returning overload).
errors::SyncError Mutex::unlock_err() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task) { lock_.unlock(); return errors::SYNC_ERR_NO_TASK; }
    if (owner_ != task) { lock_.unlock(); return errors::SYNC_ERR_NOT_OWNER; }
    if (lock_count_ == 0) { lock_.unlock(); return errors::SYNC_ERR_NOT_LOCKED; }

    if (lock_count_ > 1) {
        --lock_count_;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    if (wait_count_ > 0) {
        wake_one();
    }

    restore_priority();

    owner_ = nullptr;
    lock_count_ = 0;

    lock_.unlock();
    return errors::SYNC_ERR_OK;
}

}
}
