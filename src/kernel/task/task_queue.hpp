/// @file task_queue.hpp
/// @brief Intrusive doubly-linked list queue for TaskControlBlock scheduling.

#pragma once

#include <types.hpp>
#include <kernel/task/task_fwd.hpp>

namespace kernel {

/// @brief Intrusive doubly-linked list queue for TaskControlBlock.
/// Operations are O(1), no dynamic allocation. TCBs embed next/prev pointers.
class TaskQueue {
  public:
    TaskQueue() : head_(nullptr), tail_(nullptr), count_(0) {
    }

    /// @brief Appends a TCB to the tail of the queue (idempotent if already
    /// queued).
    void push_back(TaskControlBlock &tcb) noexcept;
    /// @brief Removes and returns the head of the queue, or nullptr if empty.
    TaskControlBlock *pop_front() noexcept;
    /// @brief Removes a specific TCB from anywhere in the queue.
    void remove(TaskControlBlock &tcb) noexcept;

    /// @brief Returns true if the queue contains no entries.
    bool empty() const noexcept {
        return count_ == 0;
    }
    /// @brief Returns the number of entries in the queue.
    uint64_t count() const noexcept {
        return count_;
    }

    /// @brief Returns the head of the queue (for iteration), or nullptr.
    TaskControlBlock *head() const noexcept {
        return head_;
    }

    /// @brief Returns true if the given TCB is physically linked in this
    ///        queue (walks the intrusive list).  Used by diagnostics to detect
    ///        in_ready_queue_ flag vs physical-link desync.
    bool contains(const TaskControlBlock &tcb) const noexcept;
    /// @brief Returns the tail of the queue (for append), or nullptr.
    TaskControlBlock *tail() const noexcept {
        return tail_;
    }

    /// @brief Resets queue to empty state without following pointers.
    ///        Safe to call even when pointers may be dangling.
    void reset() noexcept {
        head_ = nullptr;
        tail_ = nullptr;
        count_ = 0;
    }

    /// @brief Raw restore from POD data.  TCB pointers must be valid
    ///        (TCBs restored in-place across snapshot cycles).
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void set_raw(TaskControlBlock *h, TaskControlBlock *t,
                 uint64_t c) noexcept {
        head_ = h;
        tail_ = t;
        count_ = c;
    }

  private:
    TaskControlBlock *head_; ///< Pointer to the first TCB in the queue.
    TaskControlBlock *tail_; ///< Pointer to the last TCB in the queue.
    uint64_t count_;         ///< Number of TCBs currently enqueued.
};

} // namespace kernel
