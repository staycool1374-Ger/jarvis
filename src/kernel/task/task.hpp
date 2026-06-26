/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file task.hpp
/// @brief Task control block and state definitions for the kernel scheduler.

#pragma once

#include <types.hpp>
#include <kernel/vfs/vfs.hpp>
#include <signal.hpp>
#include <kernel/jarvis_config.h>

namespace kernel {

// Forward declaration for init_task_common
struct TaskControlBlock;
/// @brief Initialize common task fields after allocation.
void init_task_common(TaskControlBlock& tcb);

/// @brief Maximum payload size in bytes for an IPC message.
static constexpr size_t IPC_MAX_MSG_SIZE = CONFIG_IPC_MAX_MSG_SIZE;
/// @brief Maximum number of messages in a single queue.
static constexpr size_t IPC_MAX_QUEUE_MSG = CONFIG_IPC_MAX_QUEUE_MSG;
/// @brief Number of priority levels (0 = highest urgency).
static constexpr size_t IPC_PRIORITY_LEVELS = CONFIG_IPC_PRIORITY_LEVELS;

/// @brief A single IPC message with sender ID, type, priority, and payload.
struct Message {
    uint64_t sender_id;
    uint64_t type;
    uint64_t priority;
    uint8_t  data[IPC_MAX_MSG_SIZE];
    size_t   data_size;
    uint64_t buf_handle = 0;
};

/// @brief Forward declarations so TaskControlBlock can hold pointers.
struct MessageQueue;

namespace sync {
class Notify;
class EventGroup;
}

namespace task {
class SporadicServer;
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
#if defined(CONFIG_ARCH_X86_64)
struct TaskContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};
#elif defined(CONFIG_ARCH_AARCH64)
struct TaskContext {
    uint64_t x[31];          // X0-X30
    uint64_t sp_el0;         // SP_EL0 (user stack pointer)
    uint64_t elr_el1;        // ELR_EL1 (return address)
    uint64_t spsr_el1;       // SPSR_EL1 (processor state)
    uint64_t vector;         // exception vector number
    uint64_t error_code;     // exception error code
};
#elif defined(CONFIG_ARCH_RISCV64)
struct TaskContext {
    uint64_t ra, sp, gp, tp;
    uint64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64_t sepc;           // exception return address
    uint64_t sstatus;        // supervisor status register
    uint64_t stvec;          // trap vector base
    uint64_t vector;         // exception vector number
    uint64_t error_code;     // exception error code
};
#endif

/// @brief Task control block — represents a single thread of execution.
/// @note Includes scheduling parameters, register context, and stack info.
struct TaskControlBlock {
    static constexpr size_t STACK_SIZE = CONFIG_STACK_SIZE;
    static constexpr uint64_t KERNEL_CS = 0x08;
    static constexpr uint64_t KERNEL_SS = 0x10;
    static constexpr uint64_t USER_CS  = 0x1B;
    static constexpr uint64_t USER_SS  = 0x23;
    static constexpr uint64_t FLAGS_IF  = 0x200;
    static constexpr uint64_t TCB_MAGIC = 0x5443424D41474943ULL;

#ifdef CONFIG_DEBUG
    /// @brief One entry in the per-TCB context-switch trace ring buffer.
    struct DebugSwitchRecord {
        uint64_t    entry_addr;       ///< return address at switch_to_task() call site
        uint64_t    exit_rip;         ///< saved RIP from context (where task was last interrupted)
        TaskContext regs;             ///< saved register set from context
        uint64_t    thread_id;        ///< this task's ID
        uint64_t    consumed_ticks;   ///< executed_ticks at switch-out
    };
    static constexpr size_t DEBUG_SWITCH_RING_SIZE = 4;
    DebugSwitchRecord debug_switch_ring[DEBUG_SWITCH_RING_SIZE];
    uint64_t debug_switch_idx;
#endif

    TaskControlBlock()
        : 
#ifdef CONFIG_DEBUG
        debug_switch_idx(0),
#endif
        id(0)
        , parent_id(0)
        , state(TaskState::READY)
        , priority(0)
        , base_priority(0)
        , period_ticks(0)
        , deadline_ticks(0)
        , executed_ticks(0)
        , remaining_ticks(0)
        , exit_code(0)
        , context({})
        , kernel_stack(nullptr)
        , kernel_stack_top(0)
        , stack_phys_(0)
        , page_table_(0)
        , page_table_shared_(false)
        , stack_pdpt_phys_(0)
        , user_stack_(0)
        , user_stack_size_(0)
        , user_data(nullptr)
        , fpu_used(false)
        , fpu_state{}
        , program_break(0)
        , program_break_start(0)
        , fd_table({})
        , cwd_vnode(nullptr)
        , waiting_child_pid(0)
        , waiting_child_status(nullptr)
        , pending_signals(0)
        , alarm_ticks(0)
        , alarm_armed(false)
        , msg_queue(nullptr)
        , notify(nullptr)
        , event_group(nullptr)
        , sporadic_server(nullptr)
        , buf_list_head(0)
        , blocked_next(nullptr)
        , blocked_prev(nullptr)
        , blocked_on_queue(nullptr)
        , first_child(nullptr)
        , next_sibling(nullptr)
        , prev_sibling(nullptr)
        , num_children(0)
        {}

    uint64_t magic;
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
    /// @brief If true, this task shares the parent's user page tables
    /// (fork). free_user_pages() is skipped on cleanup/exec to avoid
    /// double-free.
    bool page_table_shared_;
    /// @brief Physical address of the private PDPT page allocated in
    /// clone() for the user stack region. Zero when not applicable.
    /// Used by cleanup() to free the private PDPT and its child PD/PT
    /// pages that would otherwise leak.
    uint64_t stack_pdpt_phys_;
    uint64_t user_stack_;
    uint64_t user_stack_size_;
    void* user_data;

    /// @brief FPU/SSE save area (FXSAVE/FXRSTOR — 512 bytes, 16-byte aligned).
    ///        Zeroed on task creation; populated lazily via #NM handler.
    bool fpu_used;
    alignas(16) uint8_t fpu_state[512];

    uint64_t program_break;
    uint64_t program_break_start;

    vfs::FdTable fd_table;
    char cwd[CONFIG_VFS_MAX_PATH];
    vfs::Vnode* cwd_vnode;

    uint64_t waiting_child_pid;
    uint64_t* waiting_child_status;

    /// @brief Signal handler table — one handler per signal number (max 32).
    ///        nullptr means default action (terminate on fatal signals).
    sighandler_t signal_handlers[MAX_SIGNAL_HANDLERS];

    /// @brief Pending signal bitmask (bit n set = signal n is pending).
    uint64_t pending_signals;

    /// @brief Alarm expiration tick (absolute tick count when alarm fires).
    uint64_t alarm_ticks;

    /// @brief True if alarm is armed.
    bool alarm_armed;

    /// @brief Pointer to the task's message queue (allocated and
    /// owned by IPC layer).
    MessageQueue* msg_queue;

    /// @brief Per-task notification object (allocated in
    /// init_task_common).
    sync::Notify* notify;

    /// @brief Per-task event-group object (allocated in
    /// init_task_common).
    sync::EventGroup* event_group;

    /// @brief Optional per-task SporadicServer for aperiodic
    /// daemon budget management (vfsd, iocd).  nullptr for normal tasks.
    /// Allocated from MemPool in init_sporadic_server().
    task::SporadicServer* sporadic_server;

    /// @brief Head of doubly-linked list of buffer handles owned by
    /// this task. -1 means the list is empty. Used by the BufferPool
    /// for zero-copy IPC.
    int32_t buf_list_head;

    /// @brief Linked-list pointers for the blocked-sender chain
    /// (singly linked via next).
    TaskControlBlock* blocked_next;
    TaskControlBlock* blocked_prev;

    /// @brief Pointer to the message queue this task is blocked on
    /// (as a sender).  Non-null only while the task is in another
    /// task's blocked_senders list.  Used by cleanup() to detach
    /// before the TCB is freed, preventing dangling pointers in the
    /// queue's list.
    MessageQueue* blocked_on_queue;

    /// @brief Process hierarchy: first child, next sibling, previous sibling.
    TaskControlBlock* first_child;
    TaskControlBlock* next_sibling;
    TaskControlBlock* prev_sibling;

    /// @brief Number of live child processes.
    uint64_t num_children;

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

    /// @brief Creates a new kernel task with the given entry and
    /// scheduling parameters.
    static TaskControlBlock* create(
        void (*entry)(),
        uint64_t priority,
        uint64_t period_ticks);

    /// @brief Creates a new user task running in ring 3 with its own
    /// page table.
    static TaskControlBlock* create_user(
        void (*entry)(),
        uint64_t priority,
        uint64_t period_ticks,
        size_t user_stack_size = 32_KiB);

    /// @brief Clones the current task — creates a child with copied
    /// context.
    /// @param regs Register save area from the interrupt (to copy
    /// RIP/RSP/regs).
    /// @return Pointer to the new child TaskControlBlock, or nullptr
    /// on failure.
    static TaskControlBlock* clone(uint64_t* regs);

    /// @brief Save the current register context into this TCB.
    /// @param rsp Reference to current stack pointer (updated on save).
    void save_context(uint64_t& rsp) noexcept;
    /// @brief Restore the saved register context into CPU registers.
    /// @param rsp Reference to restore the stack pointer into.
    void restore_context(uint64_t& rsp) noexcept;

    /// @brief Frees all resources owned by this task (FDs, pages, page tables).
    ///        Called by WAITPID after reaping a TERMINATED child.
    void cleanup() noexcept;

    /// @brief Allocates and initialises a SporadicServer for this task
    ///        (e.g. a Ring 3 daemon like vfsd / iocd).
    /// @param budget_c  Execution budget per period (ticks).
    /// @param period_t  Replenishment period (ticks).
    /// @param bg_prio   Priority level when budget exhausted.
    void init_sporadic_server(uint64_t budget_c, uint64_t period_t,
                              uint64_t bg_prio) noexcept;

    /// @brief Adds a child to this task's process hierarchy.
    void add_child(TaskControlBlock* child) noexcept;

    /// @brief Removes a child from this task's process hierarchy.
    void remove_child(TaskControlBlock* child) noexcept;

    /// @brief Finds a child by PID.
    TaskControlBlock* find_child(uint64_t pid) noexcept;
};

} // namespace kernel
