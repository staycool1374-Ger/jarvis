/// @file task_queue.cpp
/// @brief TaskQueue intrusive doubly-linked list implementation.

#include <kernel/task/task_queue.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

void TaskQueue::push_back(TaskControlBlock &tcb) noexcept {
    // Refuse to link a non-TCB (freed/overwritten) node — doing so would
    // corrupt the intrusive runq_next_/runq_prev_ links and later cause a
    // #GP when pop_front/remove walks them.
    if (!TaskControlBlock::is_valid(&tcb))
        return;
    if (tcb.in_ready_queue_)
        return;
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

TaskControlBlock *TaskQueue::pop_front() noexcept {
    // If the head pointer is non-null but the count is zero the list is
    // corrupted (e.g. a cycle produced by a stray double-enqueue).  Returning
    // early here prevents pop_front from spinning until count_ underflows and
    // hanging the scheduler inside reschedule()/next_task().
    if (!head_ || count_ == 0)
        return nullptr;
    // A freed/overwritten head (poisoned with 0xDD under CONFIG_DEBUG) must
    // not be dereferenced — skip it and treat the queue as empty rather than
    // faulting with a #GP (this was the source of the GPF in next_ptr; the
    // AllTasksRegistry applies the same guard to pri_next_/pri_prev_).
    if (!TaskControlBlock::is_valid(head_)) {
        head_ = nullptr;
        tail_ = nullptr;
        count_ = 0;
        return nullptr;
    }
    auto *tcb = head_;
    tcb->in_ready_queue_ = false;
    head_ = tcb->runq_next_;
    if (head_) {
        if (TaskControlBlock::is_valid(head_))
            head_->runq_prev_ = nullptr;
        else
            head_ = nullptr;
    } else {
        tail_ = nullptr;
    }
    tcb->runq_next_ = nullptr;
    tcb->runq_prev_ = nullptr;
    if (count_ > 0)
        --count_;
    return tcb;
}

void TaskQueue::remove(TaskControlBlock &tcb) noexcept {
    if (!tcb.in_ready_queue_)
        return;
    if (!TaskControlBlock::is_valid(&tcb))
        return;
    tcb.in_ready_queue_ = false;
    if (tcb.runq_prev_) {
        if (TaskControlBlock::is_valid(tcb.runq_prev_))
            tcb.runq_prev_->runq_next_ = tcb.runq_next_;
    } else {
        head_ = tcb.runq_next_;
    }
    if (tcb.runq_next_) {
        if (TaskControlBlock::is_valid(tcb.runq_next_))
            tcb.runq_next_->runq_prev_ = tcb.runq_prev_;
    } else {
        tail_ = tcb.runq_prev_;
    }
    tcb.runq_next_ = nullptr;
    tcb.runq_prev_ = nullptr;
    if (count_ > 0)
        --count_;
}

} // namespace kernel
