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

#include <kernel/sync/mutex.hpp>
#include <kernel/task/scheduler.hpp>
#include <assert.hpp>

namespace kernel {
namespace sync {

void Mutex::init() {
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
}

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

bool Mutex::add_waiter(TaskControlBlock& task) {
    if (wait_count_ >= MAX_WAITERS) return false;
    waiters_[wait_count_++] = &task;
    return true;
}

void Mutex::wake_one() {
    if (wait_count_ == 0) return;

    size_t best = 0;
    for (size_t i = 1; i < wait_count_; ++i) {
        if (waiters_[i]->priority > waiters_[best]->priority) best = i;
    }

    if (waiters_[best]->state != TaskState::TERMINATED)
        waiters_[best]->state = TaskState::READY;
    waiters_[best] = waiters_[--wait_count_];
}

void Mutex::inherit_priority(TaskControlBlock& waiter) {
    if (!owner_) return;
    if (waiter.priority > owner_->priority) {
        holder_priority_ = owner_->priority;
        owner_->priority = waiter.priority;
    }
}

void Mutex::restore_priority() {
    if (!owner_ || holder_priority_ == 0) return;
    owner_->priority = holder_priority_;
    holder_priority_ = 0;
}

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

    lock_.unlock();

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

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

    lock_.unlock();

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
    return errors::SYNC_ERR_OK;
}

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

void Mutex::unlock() {
    lock_.lock();
    auto* task = Scheduler::current_task();
    if (!task || owner_ != task) { lock_.unlock(); return; }

    if (lock_count_ > 1) {
        --lock_count_;
        lock_.unlock();
        return;
    }

    owner_ = nullptr;
    lock_count_ = 0;

    restore_priority();

    if (wait_count_ > 0) {
        wake_one();
    }

    lock_.unlock();
}

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

    owner_ = nullptr;
    lock_count_ = 0;

    restore_priority();

    if (wait_count_ > 0) {
        wake_one();
    }

    lock_.unlock();
    return errors::SYNC_ERR_OK;
}

}
}
