/// @file task.hpp
/// @brief Task control block and state definitions for the kernel scheduler.

#pragma once

#include <types.hpp>
#include <kernel/vfs/vfs.hpp>

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
    static constexpr uint64_t USER_CS  = 0x1B;
    static constexpr uint64_t USER_SS  = 0x23;
    static constexpr uint64_t FLAGS_IF  = 0x200;

    uint64_t id;
    uint64_t parent_id;
    TaskState state;
    uint64_t priority;
    uint64_t period_ticks;
    uint64_t deadline_ticks;
    uint64_t executed_ticks;
    uint64_t remaining_ticks;
    uint64_t exit_code;

    TaskContext context;
    uint8_t* kernel_stack;
    uint64_t kernel_stack_top;
    uint64_t stack_phys_;
    uint64_t page_table_;
    uint64_t user_stack_;
    uint64_t user_stack_size_;
    void* user_data;

    vfs::FdTable fd_table;
    char cwd[256];
    vfs::Vnode* cwd_vnode;

    uint64_t waiting_child_pid;
    uint64_t* waiting_child_status;

    /// @brief Creates a new kernel task with the given entry and scheduling parameters.
    static TaskControlBlock* create(
        void (*entry)(),
        uint64_t priority,
        uint64_t period_ticks);

    /// @brief Creates a new user task running in ring 3 with its own page table.
    static TaskControlBlock* create_user(
        void (*entry)(),
        uint64_t priority,
        uint64_t period_ticks,
        size_t user_stack_size = 32_KiB);

    /// @brief Clones the current task — creates a child with copied context.
    /// @param regs Register save area from the interrupt (to copy RIP/RSP/regs).
    /// @return Pointer to the new child TaskControlBlock, or nullptr on failure.
    static TaskControlBlock* clone(uint64_t* regs);

    void save_context(uint64_t* rsp) noexcept;
    void restore_context(uint64_t* rsp) noexcept;
};

} // namespace kernel
