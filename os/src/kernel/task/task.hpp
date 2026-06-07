/// @file task.hpp
/// @brief Task control block and state definitions for the kernel scheduler.

#pragma once

#include <types.hpp>
#include <kernel/vfs/vfs.hpp>
#include <signal.hpp>

namespace kernel {

/// @brief Maximum payload size in bytes for an IPC message.
static constexpr size_t IPC_MAX_MSG_SIZE = 64;
/// @brief Maximum number of messages in a single queue.
static constexpr size_t IPC_MAX_QUEUE_MSG = 16;
/// @brief Number of priority levels (0 = highest urgency).
static constexpr size_t IPC_PRIORITY_LEVELS = 32;

/// @brief A single IPC message with sender ID, type, priority, and payload.
struct Message {
    uint64_t sender_id;
    uint64_t type;
    uint64_t priority;
    uint8_t  data[IPC_MAX_MSG_SIZE];
    size_t   data_size;
};

/// @brief Forward declarations so TaskControlBlock can hold pointers.
struct MessageQueue;

namespace sync {
class Notify;
class EventGroup;
}



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
    uint64_t vector, error_code;
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
    uint64_t base_priority;
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

    /// @brief Signal handler table — one handler per signal number (max 32).
    ///        nullptr means default action (terminate on fatal signals).
    sighandler_t signal_handlers[MAX_SIGNAL_HANDLERS];

    /// @brief Pending signal bitmask (bit n set = signal n is pending).
    uint64_t pending_signals;

    /// @brief Pointer to the task's message queue (allocated and owned by IPC layer).
    MessageQueue* msg_queue;

    /// @brief Per-task notification object (allocated in init_task_common).
    sync::Notify* notify;

    /// @brief Per-task event-group object (allocated in init_task_common).
    sync::EventGroup* event_group;

    /// @brief Linked-list pointers for the blocked-sender chain (singly linked via next).
    TaskControlBlock* blocked_next;
    TaskControlBlock* blocked_prev;

    /// @brief Checks if the task has a handler registered for a signal.
    bool has_signal_handler(uint64_t sig) const {
        return sig < MAX_SIGNAL_HANDLERS && signal_handlers[sig] != nullptr;
    }

    /// @brief Registers a signal handler for the given signal.
    void set_signal_handler(uint64_t sig, sighandler_t handler) {
        if (sig < MAX_SIGNAL_HANDLERS) signal_handlers[sig] = handler;
    }

    /// @brief Returns the registered handler for the signal, or nullptr.
    sighandler_t get_signal_handler(uint64_t sig) const {
        return (sig < MAX_SIGNAL_HANDLERS) ? signal_handlers[sig] : nullptr;
    }

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

    /// @brief Frees all resources owned by this task (FDs, pages, page tables).
    ///        Called by WAITPID after reaping a TERMINATED child.
    void cleanup() noexcept;
};

} // namespace kernel
