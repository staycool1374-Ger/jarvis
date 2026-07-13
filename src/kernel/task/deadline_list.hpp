#pragma once

#include <types.hpp>
#include <kernel/task/task_fwd.hpp>

namespace kernel {

/// @brief Intrusive doubly-linked list sorted by deadline_ticks ascending.
///        Provides O(1) expired-detection by checking the head node only.
///        Designed to replace the linear tasks_[] deadline scan in on_tick
///        and scan_deadlines.
class DeadlineList {
public:
    DeadlineList() = default;

    /// @brief Insert a task into the list, ordered by deadline_ticks ascending.
    ///        Only meaningful for tasks with period_ticks > 0 and
    ///        deadline_ticks > 0.  O(n) where n ≤ CONFIG_MAX_TASKS (≤64).
    void insert(TaskControlBlock* t) noexcept;

    /// @brief Remove a task from the list.  Safe to call if t is not in the
    ///        list (scans for matching node).  O(n).
    void remove(TaskControlBlock* t) noexcept;

    /// @brief Remove and return the earliest-deadline task if its deadline
    ///        has expired (arch::Timer::ticks() > deadline_ticks).
    ///        Returns nullptr if the list is empty or the earliest task's
    ///        deadline has not yet passed.  O(1).
    TaskControlBlock* pop_earliest_if_expired() noexcept;

    /// @brief True if the list contains no entries.
    bool empty() const noexcept { return head_ == nullptr; }

    /// @brief Current number of entries in the list.
    uint64_t size() const noexcept { return size_; }

    /// @brief Remove all entries from the list without freeing them.
    void clear() noexcept { head_ = nullptr; size_ = 0; }

    /// @brief Returns the earliest-deadline task without removing it.
    TaskControlBlock* peek_earliest() const noexcept { return head_; }

private:
    TaskControlBlock* head_ = nullptr;
    uint64_t size_ = 0;
};

} // namespace kernel
