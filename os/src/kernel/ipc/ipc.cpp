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

#include <kernel/ipc/ipc.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/io.hpp>
#include <assert.hpp>

namespace kernel {

// ---------------------------------------------------------------------------
// MessageQueue
// ---------------------------------------------------------------------------

void MessageQueue::init() {
    head = 0;
    tail = 0;
    count = 0;
    prio_bitmap = 0;
    blocked_senders_head = nullptr;
    blocked_senders_tail = nullptr;
    lock_.unlock();  // Ensure SpinLock is unlocked (constructor not called on MemPool alloc)
}

bool MessageQueue::push(const Message& msg) {
    SpinLockGuard<sync::SpinLock> guard(lock_);
    if (is_full()) return false;

    msgs[tail] = msg;
    prio_bitmap |= (1ULL << msg.priority);
    tail = (tail + 1) % IPC_MAX_QUEUE_MSG;
    ++count;
    return true;
}

bool MessageQueue::pop(Message& out) {
    SpinLockGuard<sync::SpinLock> guard(lock_);
    if (is_empty()) return false;

    // Find message with highest priority (lowest priority number).
    // For same-priority messages, scan from head to preserve FIFO order.
    size_t best_prio = IPC_PRIORITY_LEVELS + 1;
    size_t best_idx = IPC_MAX_QUEUE_MSG;
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (head + i) % IPC_MAX_QUEUE_MSG;
        if (msgs[idx].priority < best_prio) {
            best_prio = msgs[idx].priority;
            best_idx = idx;
            if (best_prio == 0) break;
        }
    }
    if (best_idx >= IPC_MAX_QUEUE_MSG) return false;

    out = msgs[best_idx];

    // Compact: remove the gap by either advancing head (if best_idx == head)
    // or shifting trailing entries left by one.
    if (best_idx == head) {
        head = (head + 1) % IPC_MAX_QUEUE_MSG;
    } else {
        size_t pos = best_idx;
        while (true) {
            size_t next = (pos + 1) % IPC_MAX_QUEUE_MSG;
            if (next == tail) break;
            msgs[pos] = msgs[next];
            pos = next;
        }
        if (tail == 0) {
            tail = IPC_MAX_QUEUE_MSG - 1;
        } else {
            --tail;
        }
    }
    --count;

    // Rebuild priority bitmap
    prio_bitmap = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (head + i) % IPC_MAX_QUEUE_MSG;
        prio_bitmap |= (1ULL << msgs[idx].priority);
    }

    return true;
}

size_t MessageQueue::highest_priority() const {
    if (prio_bitmap == 0) return IPC_PRIORITY_LEVELS;
    return static_cast<size_t>(__builtin_ctzll(prio_bitmap));
}

// ---------------------------------------------------------------------------
// IPC
// ---------------------------------------------------------------------------

void IPC::init() {
    // Message queues are initialised per-task in create/create_user/clone.
}

bool IPC::send(uint64_t dest_id, const Message& msg, uint64_t flags) {
    auto* tcb = Scheduler::find_task(dest_id);
    if (!tcb || !tcb->msg_queue || tcb->state == TaskState::TERMINATED
        ) return false;

    // If the queue is already full, either return immediately (NONBLOCK)
    // or block the sender until space becomes available.
    if (tcb->msg_queue->is_full()) {
        if (flags & IPC_NONBLOCK) return false;

        auto* cur = Scheduler::current_task();
        if (!cur) return false;

        block_sender(*tcb->msg_queue, *cur);
        cur->state = TaskState::BLOCKED;
        Scheduler::reschedule();

        // Woken up — destination may have been cleaned up while we were
        // blocked.  Re-lookup to avoid accessing a dangling reference.
        tcb = Scheduler::find_task(dest_id);
        if (!tcb || !tcb->msg_queue || tcb->state == TaskState::TERMINATED)
            return false;

        // Queue should have space now
        if (tcb->msg_queue->is_full()) return false;
    }

    bool ok = tcb->msg_queue->push(msg);
    if (!ok) return false;

    // Zero-copy buffer transfer: if the message carries a buffer handle,
    // transfer ownership from current task to the destination.
    if (msg.buf_handle != 0) {
        auto* cur = Scheduler::current_task();
        if (cur) {
            BufferPool::transfer(msg.buf_handle, *cur, *tcb);
        }
    }

    if (tcb->state == TaskState::BLOCKED) {
        tcb->state = TaskState::READY;
    }
    return ok;
}

bool IPC::recv(Message& msg) {
    auto* cur = Scheduler::current_task();
    if (!cur || !cur->msg_queue) return false;

    bool ok = cur->msg_queue->pop(msg);
    if (ok && cur->msg_queue->blocked_senders_head) {
        wake_sender(*cur->msg_queue, *cur);
    }
    return ok;
}

bool IPC::send_sync(uint64_t dest_id, const Message& msg, Message& reply) {
    // Send the request message
    if (!send(dest_id, msg)) return false;

    // Block waiting for a reply on our own queue
    auto* cur = Scheduler::current_task();
    if (!cur || !cur->msg_queue) return false;

    bool was_blocked = false;
    while (cur->msg_queue->is_empty()) {
        // If destination died while we were waiting for a reply, bail out
        auto* dest = Scheduler::find_task(dest_id);
        if (!dest || dest->state == TaskState::TERMINATED) return false;

        cur->state = TaskState::BLOCKED;
        was_blocked = true;
        Scheduler::reschedule();
        if (cur->page_table_) {
            arch::sti();
            arch::hlt();
            arch::cli();
        }
    }
    if (was_blocked) cur->state = TaskState::READY;

    return cur->msg_queue->pop(reply);
}

MessageQueue& IPC::queue(uint64_t task_id) {
    auto* tcb = Scheduler::find_task(task_id);
    ENSURE(tcb != nullptr && tcb->msg_queue != nullptr);
    return *tcb->msg_queue;
}

bool IPC::block_sender(MessageQueue& q, TaskControlBlock& task) {
    task.state = TaskState::BLOCKED;
    task.blocked_next = nullptr;
    task.blocked_on_queue = &q;
    if (q.blocked_senders_tail) {
        q.blocked_senders_tail->blocked_next = &task;
        q.blocked_senders_tail = &task;
    } else {
        q.blocked_senders_head = &task;
        q.blocked_senders_tail = &task;
    }

    // Priority inheritance: boost queue owner if sender is more urgent
    if (q.owner && task.priority > q.owner->priority) {
        q.owner->priority = task.priority;
    }
    return true;
}

void IPC::wake_sender(MessageQueue& q, TaskControlBlock& receiver) {
    if (!q.blocked_senders_head) return;

    auto* task = q.blocked_senders_head;
    q.blocked_senders_head = task->blocked_next;
    if (!q.blocked_senders_head) {
        q.blocked_senders_tail = nullptr;
    }
    task->blocked_next = nullptr;
    task->blocked_on_queue = nullptr;
    if (task->state != TaskState::TERMINATED)
        task->state = TaskState::READY;

    // Priority inheritance: restore receiver priority based on remaining
    // blocked senders
    uint64_t max_prio = receiver.base_priority;
    auto* cur = q.blocked_senders_head;
    while (cur) {
        if (cur->priority > max_prio) max_prio = cur->priority;
        cur = cur->blocked_next;
    }
    receiver.priority = max_prio;
}

} // namespace kernel
