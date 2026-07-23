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

// PCP (Priority Ceiling Protocol) requires PI (Priority Inheritance) support.
#if CONFIG_PRIORITY_CEILING_PROTOCOL && !CONFIG_MUTEX_PIP
#error "CONFIG_PRIORITY_CEILING_PROTOCOL requires CONFIG_MUTEX_PIP"
#endif

namespace kernel {
namespace sync {

/// @brief Initialise the mutex to unlocked state.
void Mutex::init() {
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
    priority_ceiling_ = 0;
}

/// @brief Initialise the mutex to unlocked state with PCP ceiling.
void Mutex::init(uint64_t ceiling) {
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
    priority_ceiling_ = ceiling;
}

/// @brief Initialise the mutex (error-returning overload).
errors::SyncError Mutex::init_err() {
    if (initialized_) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    initialized_ = true;
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
    priority_ceiling_ = 0;
    return errors::SYNC_ERR_OK;
}

/// @brief Initialise the mutex (error-returning) with PCP ceiling.
errors::SyncError Mutex::init_err(uint64_t ceiling) {
    if (initialized_) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    initialized_ = true;
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
    priority_ceiling_ = ceiling;
    return errors::SYNC_ERR_OK;
}

/// @brief Add a task to the waiter array.
/// @return false if the array is full.
bool Mutex::add_waiter(TaskControlBlock &task) {
    for (size_t i = 0; i < wait_count_; ++i) {
        ENSURE(waiters_[i] != &task);
    }
    if (wait_count_ >= MAX_WAITERS)
        return false;
    waiters_[wait_count_++] = &task;
    return true;
}

/// @brief Wake the highest-priority waiter.
void Mutex::wake_one() {
    if (wait_count_ == 0)
        return;

    size_t best = 0;
    for (size_t i = 1; i < wait_count_; ++i) {
        if (waiters_[i] && (!waiters_[best] ||
                            waiters_[i]->priority > waiters_[best]->priority))
            best = i;
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
void Mutex::inherit_priority(TaskControlBlock &waiter) {
#if CONFIG_MUTEX_PIP
    if (!owner_)
        return;

    uint64_t max_prio = waiter.priority;
    for (size_t i = 0; i < wait_count_; ++i) {
        if (waiters_[i]->priority > max_prio)
            max_prio = waiters_[i]->priority;
    }

    if (max_prio > owner_->priority) {
        if (holder_priority_ == 0)
            holder_priority_ = owner_->priority;
        owner_->priority = max_prio;

        if (owner_->waiting_on_mutex)
            owner_->waiting_on_mutex->reevaluate();
    }
#else
    (void)waiter;
#endif
}

/// @brief Restore the owner's priority to its saved original value,
///        unless remaining waiters still need a boost.
void Mutex::restore_priority() {
#if CONFIG_MUTEX_PIP
    if (!owner_ || holder_priority_ == 0)
        return;

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
#endif
}

/// @brief Re-evaluate the owner's priority based on all current waiters.
///        Called when a waiter's priority changes (e.g. transitively boosted
///        by another mutex further down the chain).
void Mutex::reevaluate() {
#if CONFIG_MUTEX_PIP
    if (!owner_ || wait_count_ == 0)
        return;

    uint64_t max_prio = 0;
    for (size_t i = 0; i < wait_count_; ++i) {
        if (waiters_[i]->priority > max_prio)
            max_prio = waiters_[i]->priority;
    }

    if (max_prio > owner_->priority) {
        if (holder_priority_ == 0)
            holder_priority_ = owner_->priority;
        owner_->priority = max_prio;

        if (owner_->waiting_on_mutex)
            owner_->waiting_on_mutex->reevaluate();
    }
#endif
}

/// @brief Acquire the mutex, blocking until available (supports recursion).
void Mutex::lock() {
    lock_.lock();
    auto *task = Scheduler::current_task();
    if (!task) {
        lock_.unlock();
        return;
    }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return;
    }

    size_t _pcp_retry = 0;
    for (; _pcp_retry < MAX_WAITERS + 1; ++_pcp_retry) {
#if CONFIG_PRIORITY_CEILING_PROTOCOL
        if (priority_ceiling_ > 0 && task->system_ceiling_ > 0 &&
            task->priority <= task->system_ceiling_) {
            bool added = add_waiter(*task);
            ENSURE(added);
            task->waiting_on_mutex = this;
            lock_.unlock();
            task->state = TaskState::BLOCKED;
            Scheduler::reschedule();
            lock_.lock();
            task->waiting_on_mutex = nullptr;
            if (owner_ == task) {
                ++lock_count_;
                lock_.unlock();
                return;
            }
            continue;
        }
#endif
        if (owner_ == nullptr) {
            owner_ = task;
            lock_count_ = 1;
#if CONFIG_PRIORITY_CEILING_PROTOCOL
            if (priority_ceiling_ > 0 &&
                task->held_ceiling_depth_ < CONFIG_MAX_HELD_CEILINGS) {
                task->held_ceilings_[task->held_ceiling_depth_++] =
                    priority_ceiling_;
                if (priority_ceiling_ > task->system_ceiling_)
                    task->system_ceiling_ = priority_ceiling_;
            }
#endif
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
        lock_.lock();
        task->waiting_on_mutex = nullptr;
    }
}

/// @brief Acquire the mutex (error-returning overload).
errors::SyncError Mutex::lock_err() {
    lock_.lock();
    auto *task = Scheduler::current_task();
    if (!task) {
        lock_.unlock();
        return errors::SYNC_ERR_NO_TASK;
    }

    if (owner_ == task) {
        ++lock_count_;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    size_t _pcp_retry = 0;
    for (; _pcp_retry < MAX_WAITERS + 1; ++_pcp_retry) {
#if CONFIG_PRIORITY_CEILING_PROTOCOL
        if (priority_ceiling_ > 0 && task->system_ceiling_ > 0 &&
            task->priority <= task->system_ceiling_) {
            if (wait_count_ >= MAX_WAITERS) {
                lock_.unlock();
                return errors::SYNC_ERR_MAX_WAITERS;
            }
            bool added = add_waiter(*task);
            ENSURE(added);
            task->waiting_on_mutex = this;
            lock_.unlock();
            task->state = TaskState::BLOCKED;
            Scheduler::reschedule();
            lock_.lock();
            task->waiting_on_mutex = nullptr;
            if (owner_ == task) {
                ++lock_count_;
                lock_.unlock();
                return errors::SYNC_ERR_OK;
            }
            continue;
        }
#endif
        if (owner_ == nullptr) {
            owner_ = task;
            lock_count_ = 1;
#if CONFIG_PRIORITY_CEILING_PROTOCOL
            if (priority_ceiling_ > 0 &&
                task->held_ceiling_depth_ < CONFIG_MAX_HELD_CEILINGS) {
                task->held_ceilings_[task->held_ceiling_depth_++] =
                    priority_ceiling_;
                if (priority_ceiling_ > task->system_ceiling_)
                    task->system_ceiling_ = priority_ceiling_;
            }
#endif
            lock_.unlock();
            return errors::SYNC_ERR_OK;
        }

        if (wait_count_ >= MAX_WAITERS) {
            lock_.unlock();
            return errors::SYNC_ERR_MAX_WAITERS;
        }

        inherit_priority(*task);

        bool added = add_waiter(*task);
        ENSURE(added);
        task->waiting_on_mutex = this;

        lock_.unlock();

        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
        lock_.lock();
        task->waiting_on_mutex = nullptr;
    }

    lock_.unlock();
    return errors::SYNC_ERR_INTERRUPTED;
}

/// @brief Attempt to acquire without blocking.
bool Mutex::try_lock() {
    lock_.lock();
    auto *task = Scheduler::current_task();
    if (!task) {
        lock_.unlock();
        return false;
    }

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
    auto *task = Scheduler::current_task();
    if (!task) {
        lock_.unlock();
        return errors::SYNC_ERR_NO_TASK;
    }

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

/// @brief Helper: pop the mutex's ceiling from the owner's held ceiling stack
///        and recalculate system_ceiling_.
static void pop_ceiling(TaskControlBlock *owner, uint64_t ceiling) {
#if CONFIG_PRIORITY_CEILING_PROTOCOL
    if (!owner || ceiling == 0 || owner->held_ceiling_depth_ == 0)
        return;
    // Find and remove the matching ceiling entry
    bool found = false;
    for (size_t i = 0; i < owner->held_ceiling_depth_; ++i) {
        if (!found && owner->held_ceilings_[i] == ceiling) {
            found = true;
        }
        if (found && i + 1 < owner->held_ceiling_depth_) {
            owner->held_ceilings_[i] = owner->held_ceilings_[i + 1];
        }
    }
    if (found) {
        owner->held_ceiling_depth_--;
    }
    // Recalculate system ceiling
    owner->system_ceiling_ = 0;
    for (size_t i = 0; i < owner->held_ceiling_depth_; ++i) {
        if (owner->held_ceilings_[i] > owner->system_ceiling_)
            owner->system_ceiling_ = owner->held_ceilings_[i];
    }
#else
    (void)owner;
    (void)ceiling;
#endif
}

/// @brief Release the mutex, waking the next highest-priority waiter.
void Mutex::unlock() {
    lock_.lock();
    auto *task = Scheduler::current_task();
    if (!task || owner_ != task) {
        lock_.unlock();
        return;
    }

    if (lock_count_ > 1) {
        --lock_count_;
        lock_.unlock();
        return;
    }

    uint64_t released_ceiling = priority_ceiling_;

    if (wait_count_ > 0) {
        wake_one();
    }

    restore_priority();

    owner_ = nullptr;
    lock_count_ = 0;

    pop_ceiling(task, released_ceiling);

    lock_.unlock();
}

/// @brief Release the mutex (error-returning overload).
errors::SyncError Mutex::unlock_err() {
    lock_.lock();
    auto *task = Scheduler::current_task();
    if (!task) {
        lock_.unlock();
        return errors::SYNC_ERR_NO_TASK;
    }
    if (owner_ != task) {
        lock_.unlock();
        return errors::SYNC_ERR_NOT_OWNER;
    }
    if (lock_count_ == 0) {
        lock_.unlock();
        return errors::SYNC_ERR_NOT_LOCKED;
    }

    if (lock_count_ > 1) {
        --lock_count_;
        lock_.unlock();
        return errors::SYNC_ERR_OK;
    }

    uint64_t released_ceiling = priority_ceiling_;

    if (wait_count_ > 0) {
        wake_one();
    }

    restore_priority();

    owner_ = nullptr;
    lock_count_ = 0;

    pop_ceiling(task, released_ceiling);

    lock_.unlock();
    return errors::SYNC_ERR_OK;
}

} // namespace sync
} // namespace kernel
