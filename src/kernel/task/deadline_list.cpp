#include <kernel/task/deadline_list.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>

namespace kernel {

void DeadlineList::insert(TaskControlBlock *t) noexcept {
    t->dl_next_ = nullptr;
    t->dl_prev_ = nullptr;

    if (!head_) {
        head_ = t;
        ++size_;
        return;
    }

    // Find insertion point — sorted by deadline_ticks ascending.
    // If head's deadline is later than t's, t becomes new head.
    if (t->deadline_ticks < head_->deadline_ticks) {
        t->dl_next_ = head_;
        head_->dl_prev_ = t;
        head_ = t;
        ++size_;
        return;
    }

    // Scan forward to find the correct position
    TaskControlBlock *cur = head_;
    while (cur->dl_next_ &&
           cur->dl_next_->deadline_ticks <= t->deadline_ticks) {
        cur = cur->dl_next_;
    }

    t->dl_next_ = cur->dl_next_;
    t->dl_prev_ = cur;
    if (cur->dl_next_) {
        cur->dl_next_->dl_prev_ = t;
    }
    cur->dl_next_ = t;
    ++size_;
}

void DeadlineList::remove(TaskControlBlock *t) noexcept {
    if (!head_)
        return;

    // Fast path: t is the head
    if (head_ == t) {
        head_ = t->dl_next_;
        if (head_)
            head_->dl_prev_ = nullptr;
        t->dl_next_ = nullptr;
        t->dl_prev_ = nullptr;
        --size_;
        return;
    }

    // Scan for t
    TaskControlBlock *cur = head_;
    while (cur && cur != t) {
        cur = cur->dl_next_;
    }
    if (!cur)
        return; // not found

    // Unlink
    if (cur->dl_prev_)
        cur->dl_prev_->dl_next_ = cur->dl_next_;
    if (cur->dl_next_)
        cur->dl_next_->dl_prev_ = cur->dl_prev_;
    cur->dl_next_ = nullptr;
    cur->dl_prev_ = nullptr;
    --size_;
}

TaskControlBlock *DeadlineList::pop_earliest_if_expired() noexcept {
    if (!head_)
        return nullptr;

    uint64_t now = arch::Timer::ticks();
    if (now <= head_->deadline_ticks)
        return nullptr;

    // Earliest task has expired — pop it
    auto *t = head_;
    head_ = t->dl_next_;
    if (head_)
        head_->dl_prev_ = nullptr;
    t->dl_next_ = nullptr;
    t->dl_prev_ = nullptr;
    --size_;
    return t;
}

} // namespace kernel
