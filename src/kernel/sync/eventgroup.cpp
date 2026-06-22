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

#include <kernel/sync/eventgroup.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <assert.hpp>

namespace kernel {
namespace sync {

void EventGroup::init() {
    bits_ = 0;
    wait_count_ = 0;
}

void EventGroup::set_bits(uint64_t bits) {
    SpinLockGuard<SpinLock> guard(lock_);
    bits_ |= bits;
    wake_matching();
}

void EventGroup::clear_bits(uint64_t bits) {
    SpinLockGuard<SpinLock> guard(lock_);
    bits_ &= ~bits;
}

bool EventGroup::add_waiter(TaskControlBlock& task, uint64_t wanted, bool clear
    ) {
    // Caller must hold lock_ (wait_bits already holds it)
    if (wait_count_ >= MAX_WAITERS) return false;
    waiters_[wait_count_].task = &task;
    waiters_[wait_count_].wanted_bits = wanted;
    waiters_[wait_count_].clear_on_exit = clear;
    ++wait_count_;
    return true;
}

void EventGroup::wake_matching() {
    // Caller must hold lock_ (set_bits / wait_bits already hold it)
    for (size_t i = 0; i < wait_count_;) {
        if ((bits_ & waiters_[i].wanted_bits) == waiters_[i].wanted_bits) {
            if (waiters_[i].clear_on_exit) {
                bits_ &= ~waiters_[i].wanted_bits;
            }
            if (waiters_[i].task->state != TaskState::TERMINATED)
                waiters_[i].task->state = TaskState::READY;
            waiters_[i] = waiters_[--wait_count_];
        } else {
            ++i;
        }
    }
}

uint64_t EventGroup::wait_bits(uint64_t bits, bool clear_on_exit) {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return bits_;

    if ((bits_ & bits) == bits) {
        if (clear_on_exit) bits_ &= ~bits;
        return bits_;
    }

    bool added = add_waiter(*task, bits, clear_on_exit);
    ENSURE(added);

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    return bits_;
}

bool EventGroup::try_wait_bits(uint64_t bits) {
    return (bits_ & bits) == bits;
}

}
}
