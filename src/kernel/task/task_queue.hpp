#pragma once

#include <types.hpp>
#include <kernel/task/task_fwd.hpp>

namespace kernel {

class TaskQueue {
public:
    TaskQueue() : head_(nullptr), tail_(nullptr), count_(0) {}

    void push_back(TaskControlBlock& tcb) noexcept;
    TaskControlBlock* pop_front() noexcept;
    void remove(TaskControlBlock& tcb) noexcept;

    bool empty() const noexcept { return count_ == 0; }
    uint64_t count() const noexcept { return count_; }

    TaskControlBlock* head() const noexcept { return head_; }
    TaskControlBlock* tail() const noexcept { return tail_; }

    /// @brief Resets queue to empty state without following pointers.
    ///        Safe to call even when pointers may be dangling.
    void reset() noexcept { head_ = nullptr; tail_ = nullptr; count_ = 0; }

    /// @brief Raw restore from POD data.  TCB pointers must be valid
    ///        (TCBs restored in-place across snapshot cycles).
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void set_raw(TaskControlBlock* h, TaskControlBlock* t, uint64_t c) noexcept {
        head_ = h; tail_ = t; count_ = c;
    }

private:
    TaskControlBlock* head_;
    TaskControlBlock* tail_;
    uint64_t count_;
};

} // namespace kernel
