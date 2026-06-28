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

#include <kernel/sync/queue.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <string.hpp>

namespace kernel {
namespace sync {

void Queue::init() {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    send_waiters_count_ = 0;
    recv_waiters_count_ = 0;
}

errors::SyncError Queue::init_err() {
    if (count_ != 0 || send_waiters_count_ != 0 || recv_waiters_count_ != 0) {
        return errors::SYNC_ERR_ALREADY_INITIALIZED;
    }
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    send_waiters_count_ = 0;
    recv_waiters_count_ = 0;
    return errors::SYNC_ERR_OK;
}

bool Queue::add_send_waiter(TaskControlBlock* task) {
    // Caller must hold lock_
    if (send_waiters_count_ >= MAX_WAITERS) return false;
    send_waiters_[send_waiters_count_++] = task;
    return true;
}

bool Queue::add_recv_waiter(TaskControlBlock* task) {
    // Caller must hold lock_
    if (recv_waiters_count_ >= MAX_WAITERS) return false;
    recv_waiters_[recv_waiters_count_++] = task;
    return true;
}

void Queue::wake_send_one() {
    // Caller must hold lock_
    if (send_waiters_count_ == 0) return;
    size_t best = 0;
    for (size_t i = 1; i < send_waiters_count_; ++i) {
        if (send_waiters_[i]->priority > send_waiters_[best]->priority
            ) best = i;
    }
    if (send_waiters_[best]->state != TaskState::TERMINATED)
        Scheduler::set_task_ready(*send_waiters_[best]);
    send_waiters_[best] = send_waiters_[--send_waiters_count_];
}

void Queue::wake_recv_one() {
    // Caller must hold lock_
    if (recv_waiters_count_ == 0) return;
    size_t best = 0;
    for (size_t i = 1; i < recv_waiters_count_; ++i) {
        if (recv_waiters_[i]->priority > recv_waiters_[best]->priority
            ) best = i;
    }
    if (recv_waiters_[best]->state != TaskState::TERMINATED)
        Scheduler::set_task_ready(*recv_waiters_[best]);
    recv_waiters_[best] = recv_waiters_[--recv_waiters_count_];
}

bool Queue::send(const uint8_t* data, size_t size) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (size > QUEUE_MAX_MSG_SIZE) return false;

    auto* task = Scheduler::current_task();

    while (is_full()) {
        if (!add_send_waiter(task)) return false;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    memcpy(msgs_[tail_].data, data, size);
    msgs_[tail_].size = size;
    tail_ = (tail_ + 1) % QUEUE_MAX_MSG_COUNT;
    ++count_;

    wake_recv_one();

    return true;
}

errors::SyncError Queue::send_err(const uint8_t* data, size_t size, size_t* sent_bytes) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (size > QUEUE_MAX_MSG_SIZE) return errors::SYNC_ERR_MSG_TOO_LARGE;

    auto* task = Scheduler::current_task();
    if (!task) return errors::SYNC_ERR_NO_TASK;

    while (is_full()) {
        if (send_waiters_count_ >= MAX_WAITERS) return errors::SYNC_ERR_MAX_WAITERS;
        send_waiters_[send_waiters_count_++] = task;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    memcpy(msgs_[tail_].data, data, size);
    msgs_[tail_].size = size;
    tail_ = (tail_ + 1) % QUEUE_MAX_MSG_COUNT;
    ++count_;

    wake_recv_one();

    if (sent_bytes) *sent_bytes = size;
    return errors::SYNC_ERR_OK;
}

bool Queue::try_send(const uint8_t* data, size_t size) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (size > QUEUE_MAX_MSG_SIZE || is_full()) return false;

    memcpy(msgs_[tail_].data, data, size);
    msgs_[tail_].size = size;
    tail_ = (tail_ + 1) % QUEUE_MAX_MSG_COUNT;
    ++count_;

    wake_recv_one();
    return true;
}

errors::SyncError Queue::try_send_err(const uint8_t* data, size_t size, size_t* sent_bytes) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (size > QUEUE_MAX_MSG_SIZE) return errors::SYNC_ERR_MSG_TOO_LARGE;
    if (is_full()) return errors::SYNC_ERR_QUEUE_FULL;

    memcpy(msgs_[tail_].data, data, size);
    msgs_[tail_].size = size;
    tail_ = (tail_ + 1) % QUEUE_MAX_MSG_COUNT;
    ++count_;

    wake_recv_one();

    if (sent_bytes) *sent_bytes = size;
    return errors::SYNC_ERR_OK;
}

bool Queue::receive(uint8_t* buf, size_t* size) {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();

    while (is_empty()) {
        if (!add_recv_waiter(task)) return false;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    size_t copy_size = msgs_[head_].size;
    if (buf && size) {
        if (*size < copy_size) copy_size = *size;
        memcpy(buf, msgs_[head_].data, copy_size);
        *size = copy_size;
    }

    head_ = (head_ + 1) % QUEUE_MAX_MSG_COUNT;
    --count_;

    wake_send_one();
    return true;
}

errors::SyncError Queue::receive_err(uint8_t* buf, size_t* size, size_t* received_bytes) {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return errors::SYNC_ERR_NO_TASK;

    while (is_empty()) {
        if (recv_waiters_count_ >= MAX_WAITERS) return errors::SYNC_ERR_MAX_WAITERS;
        recv_waiters_[recv_waiters_count_++] = task;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    size_t copy_size = msgs_[head_].size;
    if (buf && size) {
        if (*size < copy_size) copy_size = *size;
        memcpy(buf, msgs_[head_].data, copy_size);
        *size = copy_size;
    }

    head_ = (head_ + 1) % QUEUE_MAX_MSG_COUNT;
    --count_;

    wake_send_one();

    if (received_bytes) *received_bytes = copy_size;
    return errors::SYNC_ERR_OK;
}

bool Queue::try_receive(uint8_t* buf, size_t* size) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (is_empty()) return false;

    size_t copy_size = msgs_[head_].size;
    if (buf && size) {
        if (*size < copy_size) copy_size = *size;
        memcpy(buf, msgs_[head_].data, copy_size);
        *size = copy_size;
    }

    head_ = (head_ + 1) % QUEUE_MAX_MSG_COUNT;
    --count_;

    wake_send_one();
    return true;
}

errors::SyncError Queue::try_receive_err(uint8_t* buf, size_t* size, size_t* received_bytes) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (is_empty()) return errors::SYNC_ERR_QUEUE_EMPTY;

    size_t copy_size = msgs_[head_].size;
    if (buf && size) {
        if (*size < copy_size) copy_size = *size;
        memcpy(buf, msgs_[head_].data, copy_size);
        *size = copy_size;
    }

    head_ = (head_ + 1) % QUEUE_MAX_MSG_COUNT;
    --count_;

    wake_send_one();

    if (received_bytes) *received_bytes = copy_size;
    return errors::SYNC_ERR_OK;
}

}
}
