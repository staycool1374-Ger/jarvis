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

#include <kernel/sync/notify.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/sync/spinlock_guard.hpp>

namespace kernel {
namespace sync {

void Notify::init() {
    notify_value_ = 0;
    waiter_ = nullptr;
    initialized_ = true;
}

errors::SyncError Notify::init_err() {
    if (initialized_) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    notify_value_ = 0;
    waiter_ = nullptr;
    initialized_ = true;
    return errors::SYNC_ERR_OK;
}

void Notify::notify(uint64_t value) {
    SpinLockGuard<SpinLock> guard(lock_);
    notify_value_ = value;
    if (waiter_) {
        if (waiter_->state != TaskState::TERMINATED)
            waiter_->state = TaskState::READY;
        waiter_ = nullptr;
    }
}

errors::SyncError Notify::notify_err(uint64_t value) {
    SpinLockGuard<SpinLock> guard(lock_);
    notify_value_ = value;
    if (waiter_) {
        if (waiter_->state != TaskState::TERMINATED)
            waiter_->state = TaskState::READY;
        waiter_ = nullptr;
        return errors::SYNC_ERR_OK;
    }
    return errors::SYNC_ERR_NO_WAITER;
}

uint64_t Notify::wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return 0;

    if (waiter_ != nullptr) return 0;

    waiter_ = task;
    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    return notify_value_;
}

errors::SyncError Notify::wait_err(uint64_t* out_value) {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return errors::SYNC_ERR_NO_TASK;

    if (waiter_ != nullptr) {
        return errors::SYNC_ERR_ALREADY_WAITING;
    }

    waiter_ = task;
    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    if (out_value) *out_value = notify_value_;
    return errors::SYNC_ERR_OK;
}

bool Notify::try_wait(uint64_t* value) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_ == nullptr && value && notify_value_ != 0) {
        *value = notify_value_;
        notify_value_ = 0;
        return true;
    }
    return false;
}

errors::SyncError Notify::try_wait_err(uint64_t* value) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_ == nullptr && value && notify_value_ != 0) {
        *value = notify_value_;
        notify_value_ = 0;
        return errors::SYNC_ERR_OK;
    }
    return errors::SYNC_ERR_BUFFER_EMPTY;
}

}
}
