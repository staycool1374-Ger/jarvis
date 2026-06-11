#include <kernel/ipc/ipc.hpp>
#include <kernel/task/scheduler.hpp>
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
}

bool MessageQueue::push(const Message& msg) {
    if (is_full()) return false;

    msgs[tail] = msg;
    prio_bitmap |= (1ULL << msg.priority);
    tail = (tail + 1) % IPC_MAX_QUEUE_MSG;
    ++count;
    return true;
}

bool MessageQueue::pop(Message& out) {
    if (is_empty()) return false;

    // Find message with highest priority (lowest priority number).
    // For same-priority messages, scan from head to preserve FIFO order.
    size_t best_prio = IPC_PRIORITY_LEVELS;
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

    // Compact: shift all entries after best_idx left by one
    size_t pos = best_idx;
    while (pos != tail) {
        size_t next = (pos + 1) % IPC_MAX_QUEUE_MSG;
        if (next == head) break;  // wrapped fully around
        msgs[pos] = msgs[next];
        pos = next;
        if (pos == tail) {
            // We've shifted everything; tail moves back one
            break;
        }
    }
    // Move tail back
    if (tail == 0) {
        tail = IPC_MAX_QUEUE_MSG - 1;
    } else {
        --tail;
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
    if (!tcb || !tcb->msg_queue) return false;

    MessageQueue& q = *tcb->msg_queue;

    // If the queue is already full, either return immediately (NONBLOCK)
    // or block the sender until space becomes available.
    if (q.is_full()) {
        if (flags & IPC_NONBLOCK) return false;

        auto* cur = Scheduler::current_task();
        if (!cur) return false;

        block_sender(q, cur);
        cur->state = TaskState::BLOCKED;
        Scheduler::reschedule();

        // Woken up — queue should have space now
        if (q.is_full()) return false;
    }

    bool ok = q.push(msg);
    if (ok && tcb->state == TaskState::BLOCKED) {
        tcb->state = TaskState::READY;
    }
    return ok;
}

bool IPC::recv(Message& msg) {
    auto* cur = Scheduler::current_task();
    if (!cur || !cur->msg_queue) return false;

    bool ok = cur->msg_queue->pop(msg);
    if (ok && cur->msg_queue->blocked_senders_head) {
        wake_sender(*cur->msg_queue, cur);
    }
    return ok;
}

bool IPC::send_sync(uint64_t dest_id, const Message& msg, Message& reply) {
    // Send the request message
    if (!send(dest_id, msg)) return false;

    // Block waiting for a reply on our own queue
    auto* cur = Scheduler::current_task();
    if (!cur || !cur->msg_queue) return false;

    while (cur->msg_queue->is_empty()) {
        cur->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    return cur->msg_queue->pop(reply);
}

MessageQueue& IPC::queue(uint64_t task_id) {
    auto* tcb = Scheduler::find_task(task_id);
    ENSURE(tcb != nullptr && tcb->msg_queue != nullptr);
    return *tcb->msg_queue;
}

bool IPC::block_sender(MessageQueue& q, TaskControlBlock* task) {
    task->blocked_next = nullptr;
    if (q.blocked_senders_tail) {
        q.blocked_senders_tail->blocked_next = task;
        q.blocked_senders_tail = task;
    } else {
        q.blocked_senders_head = task;
        q.blocked_senders_tail = task;
    }

    // Priority inheritance: boost queue owner if sender is more urgent
    if (q.owner && task->priority > q.owner->priority) {
        q.owner->priority = task->priority;
    }
    return true;
}

void IPC::wake_sender(MessageQueue& q, TaskControlBlock* receiver) {
    if (!q.blocked_senders_head) return;

    auto* task = q.blocked_senders_head;
    q.blocked_senders_head = task->blocked_next;
    if (!q.blocked_senders_head) {
        q.blocked_senders_tail = nullptr;
    }
    task->blocked_next = nullptr;
    if (task->state != TaskState::TERMINATED)
        task->state = TaskState::READY;

    // Priority inheritance: restore receiver priority based on remaining blocked senders
    if (!receiver) return;
    uint64_t max_prio = receiver->base_priority;
    auto* cur = q.blocked_senders_head;
    while (cur) {
        if (cur->priority > max_prio) max_prio = cur->priority;
        cur = cur->blocked_next;
    }
    receiver->priority = max_prio;
}

} // namespace kernel
