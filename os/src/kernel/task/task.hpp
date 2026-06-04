/// @file task.hpp
/// @brief Task control block and state definitions for the kernel scheduler.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief States a task can be in during its lifecycle.
enum class TaskState : uint8_t {
    READY,
    RUNNING,
    BLOCKED,
    WAITING,
    TERMINATED,
};

/// @brief CPU register save area for context switching.
struct TaskContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, cs, rflags, rsp, ss;
};

/// @brief Task control block — represents a single thread of execution.
/// @note Includes scheduling parameters, register context, and stack info.
struct TaskControlBlock {
    static constexpr size_t STACK_SIZE = 16_KiB;
    static constexpr uint64_t KERNEL_CS = 0x08;
    static constexpr uint64_t KERNEL_SS = 0x10;
    static constexpr uint64_t FLAGS_IF  = 0x200;

    uint64_t id;
    TaskState state;
    uint64_t priority;
    uint64_t period_ticks;
    uint64_t deadline_ticks;
    uint64_t executed_ticks;
    uint64_t remaining_ticks;

    TaskContext context;
    uint8_t* kernel_stack;
    uint64_t kernel_stack_top;
    uint64_t stack_phys_;
    void* user_data;

    /// @brief Creates a new task with the given entry and scheduling parameters.
    /// @param entry       Function pointer to the task entry point.
    /// @param priority    Scheduling priority (higher = more urgent).
    /// @param period_ticks Period in timer ticks (0 = aperiodic).
    /// @return Pointer to the new TaskControlBlock, or nullptr on failure.
    static TaskControlBlock* create(
        void (*entry)(),
        uint64_t priority,
        uint64_t period_ticks);

    /// @brief Saves the current CPU registers into this task's context.
    /// @param rsp Pointer to the current stack pointer to save.
    void save_context(uint64_t* rsp) noexcept;
    /// @brief Restores this task's CPU registers from its saved context.
    /// @param rsp Pointer to restore the stack pointer into.
    void restore_context(uint64_t* rsp) noexcept;
};

} // namespace kernel
