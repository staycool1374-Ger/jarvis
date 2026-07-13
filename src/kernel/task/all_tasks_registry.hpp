#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>
#include <kernel/task/priority_map.hpp>
#include <kernel/task/task_fwd.hpp>

namespace kernel {

/// @brief Bitmap-backed, per-priority intrusive list of all registered tasks.
///        Provides O(1) insert/remove and O(1) highest-priority query.
///        Intended to replace the linear tasks_[] array in the scheduler.
class AllTasksRegistry {
    static constexpr uint64_t NUM_PRIORITIES = CONFIG_PRIORITY_CEILING + 1;

public:
    AllTasksRegistry() : heads_{}, tails_{}, total_(0) {}

    /// @brief Append a task to its priority-level list. O(1).
    void append(TaskControlBlock* t) noexcept;

    /// @brief Remove a task from its priority-level list. O(1).
    void remove(TaskControlBlock* t) noexcept;

    /// @brief Returns the head of the list for a given priority, or nullptr.
    TaskControlBlock* head(uint64_t prio) const noexcept {
        if (prio > CONFIG_PRIORITY_CEILING) return nullptr;
        return heads_[prio];
    }

    /// @brief Returns the highest priority that has at least one task. O(1).
    ///        0 if no tasks are registered.
    uint64_t highest_priority() const noexcept {
        return bitmap_.get_highest_priority();
    }

    /// @brief True if a given priority level has any tasks.
    bool has_priority(uint64_t prio) const noexcept {
        return bitmap_.is_set(prio);
    }

    /// @brief Total number of registered tasks.
    uint64_t size() const noexcept { return total_; }

    /// @brief True if no tasks are registered.
    bool empty() const noexcept { return total_ == 0; }

    /// @brief Iterate to the first task (highest priority, then insertion order).
    ///        Returns false if empty (out is unchanged).
    bool first(TaskControlBlock*& out) const noexcept;

    /// @brief Advance to the next task after @p t (same priority then next lower).
    ///        Returns false if @p t was the last task (t is unchanged).
    bool next(TaskControlBlock*& t) const noexcept;

    /// @brief Pointer-returning first() — returns nullptr if empty.
    TaskControlBlock* first_ptr() const noexcept;

    /// @brief Pointer-returning next() — returns nullptr if none follow @p t.
    TaskControlBlock* next_ptr(TaskControlBlock* t) const noexcept;

    /// @brief Fill an output array with all task pointers (for snapshot capture).
    void capture(TaskControlBlock** out, uint64_t max) const noexcept;

    /// @brief Rebuild from an array of task pointers (for snapshot restore).
    void restore(TaskControlBlock* const* in, uint64_t count) noexcept;

    /// @brief Remove all entries without freeing them.
    void clear() noexcept;

    /// @brief Rebuild per-priority linked lists using each task's current priority.
    ///        Must be called after restoring per-task priority fields from a snapshot,
    ///        because restore() reads stale priority values at insertion time.
    void rebuild() noexcept;

private:
    TaskControlBlock* heads_[NUM_PRIORITIES]{};
    TaskControlBlock* tails_[NUM_PRIORITIES]{};
    PriorityMap bitmap_;
    uint64_t total_ = 0;
};

} // namespace kernel
