#include <kernel/task/task_queue.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

void TaskQueue::push_back(TaskControlBlock& tcb) noexcept {
    if (tcb.in_ready_queue_) return;
    tcb.in_ready_queue_ = true;
    tcb.runq_next_ = nullptr;
    tcb.runq_prev_ = tail_;
    if (tail_) {
        tail_->runq_next_ = &tcb;
    } else {
        head_ = &tcb;
    }
    tail_ = &tcb;
    ++count_;
}

TaskControlBlock* TaskQueue::pop_front() noexcept {
    if (!head_) return nullptr;
    auto* tcb = head_;
    tcb->in_ready_queue_ = false;
    head_ = tcb->runq_next_;
    if (head_) {
        head_->runq_prev_ = nullptr;
    } else {
        tail_ = nullptr;
    }
    tcb->runq_next_ = nullptr;
    tcb->runq_prev_ = nullptr;
    --count_;
    return tcb;
}

void TaskQueue::remove(TaskControlBlock& tcb) noexcept {
    if (!tcb.in_ready_queue_) return;
    tcb.in_ready_queue_ = false;
    if (tcb.runq_prev_) {
        tcb.runq_prev_->runq_next_ = tcb.runq_next_;
    } else {
        head_ = tcb.runq_next_;
    }
    if (tcb.runq_next_) {
        tcb.runq_next_->runq_prev_ = tcb.runq_prev_;
    } else {
        tail_ = tcb.runq_prev_;
    }
    tcb.runq_next_ = nullptr;
    tcb.runq_prev_ = nullptr;
    --count_;
}

} // namespace kernel
