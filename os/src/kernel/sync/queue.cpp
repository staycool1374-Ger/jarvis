#include <kernel/sync/queue.hpp>
#include <kernel/task/scheduler.hpp>
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

bool Queue::add_send_waiter(TaskControlBlock* task) {
    if (send_waiters_count_ >= MAX_WAITERS) return false;
    send_waiters_[send_waiters_count_++] = task;
    return true;
}

bool Queue::add_recv_waiter(TaskControlBlock* task) {
    if (recv_waiters_count_ >= MAX_WAITERS) return false;
    recv_waiters_[recv_waiters_count_++] = task;
    return true;
}

void Queue::wake_send_one() {
    if (send_waiters_count_ == 0) return;
    size_t best = 0;
    for (size_t i = 1; i < send_waiters_count_; ++i) {
        if (send_waiters_[i]->priority > send_waiters_[best]->priority
            ) best = i;
    }
    if (send_waiters_[best]->state != TaskState::TERMINATED)
        send_waiters_[best]->state = TaskState::READY;
    send_waiters_[best] = send_waiters_[--send_waiters_count_];
}

void Queue::wake_recv_one() {
    if (recv_waiters_count_ == 0) return;
    size_t best = 0;
    for (size_t i = 1; i < recv_waiters_count_; ++i) {
        if (recv_waiters_[i]->priority > recv_waiters_[best]->priority
            ) best = i;
    }
    if (recv_waiters_[best]->state != TaskState::TERMINATED)
        recv_waiters_[best]->state = TaskState::READY;
    recv_waiters_[best] = recv_waiters_[--recv_waiters_count_];
}

bool Queue::send(const uint8_t* data, size_t size) {
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

bool Queue::try_send(const uint8_t* data, size_t size) {
    if (size > QUEUE_MAX_MSG_SIZE || is_full()) return false;

    memcpy(msgs_[tail_].data, data, size);
    msgs_[tail_].size = size;
    tail_ = (tail_ + 1) % QUEUE_MAX_MSG_COUNT;
    ++count_;

    wake_recv_one();
    return true;
}

bool Queue::receive(uint8_t* buf, size_t* size) {
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

bool Queue::try_receive(uint8_t* buf, size_t* size) {
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

}
}
