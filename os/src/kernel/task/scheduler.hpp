/// @file scheduler.hpp
/// @brief Rate-monotonic scheduler for task management and dispatching.

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

/// @brief Preemptive, rate-monotonic scheduler managing up to MAX_TASKS tasks.
/// @note Scheduler is tick-driven and supports periodic and aperiodic tasks.
class Scheduler {
public:
    /// @brief Initialises the scheduler and creates the idle task.
    static void init();

    /// @brief Finds a task by its ID (hash table, O(1) amortized).
    /// @param id Task ID to find.
    /// @return Pointer to the TaskControlBlock, or nullptr.
    static TaskControlBlock* find_task(uint64_t id) noexcept;

    /// @brief Adds a task to the scheduler's run queue.
    /// @param task Reference to the task to add.
    static void add_task(TaskControlBlock& task);
    /// @brief Removes a task from the scheduler's run queue.
    /// @param task Reference to the task to remove.
    static void remove_task(TaskControlBlock& task);

    /// @brief Returns the currently running task.
    /// @return Pointer to the current TaskControlBlock.
    static TaskControlBlock* current_task() noexcept;
    /// @brief Returns the total number of managed tasks.
    /// @return Task count.
    static uint64_t task_count() noexcept;
    /// @brief Returns the task at a given index in the task array.
    /// @param index Index into the task array.
    /// @return Pointer to the TaskControlBlock, or nullptr.
    static TaskControlBlock* task_at(uint64_t index) noexcept;

    /// @brief Called on each timer tick; updates scheduling state.
    static void on_tick() noexcept;

    /// @brief Forces a reschedule — selects the next task and sets
    /// up context switch.
    static void reschedule() noexcept;

    /// @brief Reaps orphan TERMINATED tasks (no parent to WAITPID them).
    static void reap_orphans() noexcept;

    /// @brief Terminates and removes all non-idle tasks from the scheduler.
    ///        Called after the boot-time test suite to clean up test leftovers
    ///        before production tasks (shell, idle) are created.
    static void cleanup_test_tasks() noexcept;

    /// @brief Checks if a context switch is needed (reschedule flag).
    /// @return True if a switch is pending.
    static bool needs_switch() noexcept;
    /// @brief Selects the next task to run according to RM policy.
    /// @return Pointer to the next TaskControlBlock.
    static TaskControlBlock* next_task() noexcept;
    /// @brief Sets the current running task.
    /// @param task Reference to the task to set as current.
    static void set_current(TaskControlBlock& task) noexcept;
    /// @brief Sets the current running task by ID (used after context switch).
    /// @param id Task ID to set as current.
    static void set_current_by_id(uint64_t id) noexcept;

    /// @brief Allocate a unique task ID from the ID table.
    /// @return A new task ID, or TASK_INVALID if the table is full.
    [[nodiscard]] static uint64_t alloc_id() noexcept;

    static uint64_t current_index() noexcept { return current_index_; }
    static TaskControlBlock* get_idle_task() noexcept { return idle_task_; }

    /// @brief Returns whether the scheduler can be preempted.
    /// @return True if preemption is enabled.
    static bool is_preemptible() noexcept { return preempt_enabled_; }
    /// @brief Enables or disables preemption.
    /// @param en True to enable preemption.
    static void set_preemptible(bool en) noexcept { preempt_enabled_ = en; }

private:
    static constexpr uint64_t MAX_TASKS = 64;
    static constexpr uint64_t ID_TABLE_SIZE = 128;
    static constexpr uint64_t ID_TABLE_MASK = ID_TABLE_SIZE - 1;

    /// @brief Sentinel value for a removed hash-table entry.
    static TaskControlBlock* const ID_TOMBSTONE;

    static TaskControlBlock* tasks_[MAX_TASKS];
    static TaskControlBlock* id_table_[ID_TABLE_SIZE];
    static uint64_t task_count_;
    static uint64_t current_index_;
    static uint64_t next_task_id_;
    static bool preempt_enabled_;

    static TaskControlBlock* idle_task_;

    /// @brief Performs rate-monotonic scheduling decision.
    static void rate_monotonic_schedule() noexcept;

    /// @brief Hash-table helpers for O(1) task-ID→TCB lookup.
    static uint64_t id_table_probe(uint64_t id);
    static void     id_table_insert(uint64_t id, TaskControlBlock* tcb);
    static void     id_table_remove(TaskControlBlock* task);
    static TaskControlBlock* id_table_find(uint64_t id);
};

extern "C" {
    /// @brief Address to save the current RSP into during context switch.
    extern uint64_t volatile* scheduler_save_rsp_to;
    /// @brief RSP value to load during context switch restore.
    extern uint64_t volatile scheduler_load_rsp_from;
    /// @brief CR3 value to load during context switch restore (0 = don't load).
    extern uint64_t volatile scheduler_load_cr3_from;
    /// @brief Task ID to set as current after the context switch completes.
    extern uint64_t volatile scheduler_next_task_id;
}

} // namespace kernel
