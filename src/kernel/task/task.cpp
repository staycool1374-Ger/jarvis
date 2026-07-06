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

/// @file task.cpp
/// @brief TaskControlBlock lifecycle: creation, initialisation, cloning, and cleanup.

#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/sporadic_server.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <assert.hpp>
#include <kernel/task/task_errors.hpp>
#include <string.hpp>
#include <constants.hpp>
#include <kernel/arch/io.hpp>

namespace kernel {

/// @brief Trampoline wrapper for kernel task entry points.
/// Set as RIP in the ISR save frame; RDI (first argument) is set to the
/// actual entry function. After entry() returns, the task enters a hlt
/// loop instead of crashing via ret-into-garbage.
static void _task_trampoline(void (*entry)()) {
    entry();
    for (;;) arch::hlt();
}

/// @brief Allocates and initialises a SporadicServer for a daemon task
///        from the MemPool.  Idempotent (returns early if already set).
void TaskControlBlock::init_sporadic_server(uint64_t budget_c,
                                            uint64_t period_t,
                                            uint64_t bg_prio,
                                            uint64_t budget_granularity) noexcept {
    if (sporadic_server) return;
    auto* ss = static_cast<task::SporadicServer*>(
        MemPool::alloc(sizeof(task::SporadicServer)));
    if (!ss) return;
    memset(ss, 0, sizeof(task::SporadicServer));
    ss->init(budget_c, period_t, bg_prio, budget_granularity);
    ss->set_base_priority(priority);
    sporadic_server = ss;
    Scheduler::inc_sporadic_count();
}

/// @brief Initialises common fields of a newly allocated TaskControlBlock.
/// Sets up fd table, cwd, IPC objects (MessageQueue, Notify, EventGroup),
/// process-hierarchy links, signal handlers, and buffer pool state.
void init_task_common(TaskControlBlock& tcb) {
    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        tcb.fd_table.fds[i].used = false;
        tcb.fd_table.fds[i].vnode = nullptr;
        tcb.fd_table.fds[i].offset = 0;
        tcb.fd_table.fds[i].flags = 0;
    }
    tcb.cwd[0] = '/';
    tcb.cwd[1] = '\0';
    tcb.cwd_vnode = vfs::get_root_vnode();

    // Initialize IPC message queue
    auto* mq = static_cast<MessageQueue*>(MemPool::alloc(sizeof(MessageQueue)));
    if (mq) {
        mq->init();
        mq->owner = &tcb;
        tcb.msg_queue = mq;
        kernel::test::ResourceTracker::instance().track_msg_queue_add();
    } else {
        tcb.msg_queue = nullptr;
    }
    tcb.blocked_next = nullptr;
    tcb.blocked_prev = nullptr;
    tcb.blocked_on_queue = nullptr;
    tcb.stack_pdpt_phys_ = 0;
    tcb.page_table_shared_ = false;
    tcb.user_stack_ = 0;
    tcb.user_stack_size_ = 0;

    auto* n = static_cast<sync::Notify*>(MemPool::alloc(sizeof(sync::Notify)));
    if (n) {
        n->init();
        tcb.notify = n;
        kernel::test::ResourceTracker::instance().track_notify_add();
    } else {
        tcb.notify = nullptr;
    }

    auto* eg = static_cast<sync::EventGroup*>(MemPool::alloc(sizeof(sync::
        EventGroup)));
    if (eg) {
        eg->init();
        tcb.event_group = eg;
        kernel::test::ResourceTracker::instance().track_event_group_add();
    } else {
        tcb.event_group = nullptr;
    }

    // Process hierarchy initialization
    tcb.first_child = nullptr;
    tcb.next_sibling = nullptr;
    tcb.prev_sibling = nullptr;
    tcb.num_children = 0;

    // Buffer pool list starts empty
    tcb.buf_list_head = -1;

    // Signal handlers all default to nullptr (SIG_DFL)
    for (size_t i = 0; i < MAX_SIGNAL_HANDLERS; ++i) {
        tcb.signal_handlers[i] = nullptr;
    }
    tcb.pending_signals = 0;
}

/// @brief Creates a new kernel-space TaskControlBlock.
/// Allocates a TCB from MemPool, sets up a kernel stack with an
/// architecture-specific initial register frame, and returns it.
/// @param entry  Kernel function pointer to execute.
/// @param priority  Initial scheduling priority.
/// @param period_ticks  Period for rate-monotonic scheduling.
/// @return  Pointer to the new TCB, or nullptr on OOM.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
TaskControlBlock* TaskControlBlock::create(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    Logger::raw_write("[TCB] create pool8=");
    Logger::print_dec(MemPool::pool_free_count(8));
    Logger::raw_write(" tcnt=");
    Logger::print_dec(Scheduler::task_count());
    Logger::raw_write("\n");
    auto* tcb = static_cast<TaskControlBlock*>(MemPool::alloc(sizeof(
        TaskControlBlock)));
    if (!tcb) {
        Logger::raw_write("TCB::create OOM pool8=");
        Logger::print_dec(MemPool::pool_free_count(8));
        Logger::raw_write("/8 tcnt=");
        Logger::print_dec(Scheduler::task_count());
        Logger::raw_write("\n");
        return nullptr;
    }
    memset(tcb, 0, sizeof(TaskControlBlock));

    tcb->magic = TCB_MAGIC;
    tcb->id = Scheduler::alloc_id();
    {
        char buf[CONFIG_TASK_NAME_LEN];
        size_t pos = 0;
        uint64_t n = tcb->id;
        buf[pos++] = 't';
        buf[pos++] = 'a';
        buf[pos++] = 's';
        buf[pos++] = 'k';
        buf[pos++] = '_';
        char rev[8];
        size_t rp = 0;
        do { rev[rp++] = static_cast<char>('0' + (n % 10)); n /= 10; } while (n);
        while (rp > 0 && pos < CONFIG_TASK_NAME_LEN - 1) buf[pos++] = rev[--rp];
        buf[pos] = '\0';
        __builtin_memcpy(tcb->name, buf, pos + 1);
    }
    tcb->state = TaskState::READY;
    tcb->priority = priority;
    tcb->base_priority = priority;
    tcb->period_ticks = period_ticks;
    tcb->deadline_ticks = period_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = period_ticks;
    init_task_common(*tcb);

    size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t stack_phys = PMM::alloc_contiguous(stack_pages);
    if (!stack_phys) {
        Logger::warn("TCB::create: PMM OOM for %zu-page stack", stack_pages);
        MemPool::free(tcb);
        return nullptr;
    }

    tcb->stack_phys_ = stack_phys;
    uint64_t stack_virt = arch::HHDM_OFFSET + stack_phys;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(stack_virt);
    tcb->kernel_stack_top = stack_virt + STACK_SIZE;

#if defined(CONFIG_ARCH_X86_64)
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);

    *--stack = arch::SEG_KERNEL_DATA;
    *--stack = tcb->kernel_stack_top;

    *--stack = arch::RFLAGS_DEFAULT;
    *--stack = arch::SEG_KERNEL_CODE;
    *--stack = reinterpret_cast<uint64_t>(_task_trampoline);

    *--stack = 0;
    *--stack = 0;

    // Register save frame matching isr_common push order
    *--stack = 0;                               // r15
    *--stack = 0;                               // r14
    *--stack = 0;                               // r13
    *--stack = 0;                               // r12
    *--stack = 0;                               // r11
    *--stack = 0;                               // r10
    *--stack = 0;                               // r9
    *--stack = 0;                               // r8
    *--stack = 0;                               // rbp
    *--stack = reinterpret_cast<uint64_t>(entry); // rdi = entry (SysV ABI arg1)
    *--stack = 0;                               // rsi
    *--stack = 0;                               // rdx
    *--stack = 0;                               // rcx
    *--stack = 0;                               // rbx
    *--stack = 0;                               // rax

    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_AARCH64)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = 0;                  // padding
    *--stack = 0;                  // padding
    *--stack = 0x3C5;              // SPSR_EL1: EL1h, all interrupts masked
    *--stack = reinterpret_cast<uint64_t>(entry);  // ELR_EL1
    *--stack = 0;                  // SP_EL0 (unused for kernel task)
    for (int i = 0; i < 31; ++i) *--stack = 0;     // X0-X30
    tcb->context.sp_el0 = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_RISCV64)
    // Build trap frame matching syscall_entry.S save area layout.
    // Offsets (in qwords): [0]=RA, [1]=SP, [9]=A0, [13]=A4 (regs ptr),
    //   [31]=SEPC, [32]=SSTATUS. SAVE_SIZE = 37 qwords (296 bytes).
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    stack -= 37;
    __builtin_memset(stack, 0, 37 * 8);
    stack[31] = reinterpret_cast<uint64_t>(entry);  // SEPC = entry point
    stack[32] = (1ULL << 5) | (1ULL << 8);          // SSTATUS: SPIE=1, SPP=1 (S-mode)
    tcb->context.sp = reinterpret_cast<uint64_t>(stack);
#endif

    return tcb;
}

/// @brief Creates a new user-space TaskControlBlock.
/// Allocates a TCB, kernel stack, user stack, and a cloned PML4 page table.
/// The initial register frame targets user-mode execution.
/// @param entry  User-space entry point address.
/// @param priority  Initial scheduling priority.
/// @param period_ticks  Period for rate-monotonic scheduling.
/// @param user_stack_size  Size of the user stack in bytes.
/// @return  Pointer to the new TCB, or nullptr on OOM.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
TaskControlBlock* TaskControlBlock::create_user(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks,
    size_t user_stack_size)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    auto* tcb = static_cast<TaskControlBlock*>(MemPool::alloc(sizeof(
        TaskControlBlock)));
    if (!tcb) return nullptr;
    memset(tcb, 0, sizeof(TaskControlBlock));

    tcb->magic = TCB_MAGIC;
    tcb->id = Scheduler::alloc_id();
    tcb->state = TaskState::READY;
    tcb->priority = priority;
    tcb->base_priority = priority;
    tcb->period_ticks = period_ticks;
    tcb->deadline_ticks = period_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = period_ticks;
    init_task_common(*tcb);

    size_t kernel_stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t kstack_phys = PMM::alloc_contiguous(kernel_stack_pages);
    if (!kstack_phys) { ASSERT(errors::TaskError::TASK_ERR_STACK_ALLOC
        ); MemPool::free(tcb); return nullptr; }
    tcb->stack_phys_ = kstack_phys;

    uint64_t kstack_virt = arch::HHDM_OFFSET + kstack_phys;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_virt);
    tcb->kernel_stack_top = kstack_virt + STACK_SIZE;

    size_t user_stack_pages = (user_stack_size + 4095) / arch::PAGE_SIZE;
    uint64_t ustack_phys = PMM::alloc_user_contiguous(user_stack_pages);
    if (!ustack_phys) { ASSERT(errors::TaskError::TASK_ERR_USTACK_ALLOC
        ); MemPool::free(tcb); return nullptr; }

    uint64_t pml4 = VMM::clone_kernel_pml4();
    if (!pml4) { ASSERT(errors::TaskError::TASK_ERR_PML4_CLONE); MemPool::free(
        tcb); return nullptr; }
    tcb->page_table_ = pml4;

    // Guard page: leave first page unmapped, start mapping at +arch::PAGE_SIZE
    uint64_t user_stack_virt = mem::STACK_VADDR + arch::PAGE_SIZE;
    for (size_t i = 0; i < user_stack_pages; ++i) {
        VMM::map_page_in_pml4(user_stack_virt + i * arch::PAGE_SIZE,
                              ustack_phys + i * arch::PAGE_SIZE,
                              true, pml4);
    }

    tcb->user_stack_ = ustack_phys;
    tcb->user_stack_size_ = user_stack_size;
    uint64_t user_rsp = user_stack_virt + user_stack_size;

#if defined(CONFIG_ARCH_X86_64)
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = arch::SEG_USER_DATA;
    *--stack = user_rsp;
    *--stack = arch::RFLAGS_DEFAULT;
    *--stack = arch::SEG_USER_CODE;
    *--stack = reinterpret_cast<uint64_t>(_task_trampoline);
    *--stack = 0;
    *--stack = 0;
    // Register save frame matching isr_common push order
    *--stack = 0;                               // r15
    *--stack = 0;                               // r14
    *--stack = 0;                               // r13
    *--stack = 0;                               // r12
    *--stack = 0;                               // r11
    *--stack = 0;                               // r10
    *--stack = 0;                               // r9
    *--stack = 0;                               // r8
    *--stack = 0;                               // rbp
    *--stack = reinterpret_cast<uint64_t>(entry); // rdi = entry
    *--stack = 0;                               // rsi
    *--stack = 0;                               // rdx
    *--stack = 0;                               // rcx
    *--stack = 0;                               // rbx
    *--stack = 0;                               // rax
    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_AARCH64)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = 0;                  // padding
    *--stack = 0;                  // padding
    *--stack = 0x10;               // SPSR_EL1: EL0t, all interrupts unmasked (DAIF=0)
    *--stack = reinterpret_cast<uint64_t>(entry);  // ELR_EL1
    *--stack = user_rsp;           // SP_EL0
    for (int i = 0; i < 31; ++i) *--stack = 0;     // X0-X30
    tcb->context.sp_el0 = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_RISCV64)
    // Build trap frame matching syscall_entry.S save area layout
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    stack -= 37;
    __builtin_memset(stack, 0, 37 * 8);
    stack[1] = user_rsp;                           // X2/SP = user stack
    stack[31] = reinterpret_cast<uint64_t>(entry);  // SEPC = entry point
    stack[32] = (1ULL << 5);                        // SSTATUS: SPIE=1, SPP=0 (U-mode)
    tcb->context.sp = reinterpret_cast<uint64_t>(stack);
#endif

    return tcb;
}

#if defined(CONFIG_ARCH_X86_64)
void TaskControlBlock::save_context(uint64_t& rsp) noexcept {
    context.rsp = rsp;
}

void TaskControlBlock::restore_context(uint64_t& rsp) noexcept {
    rsp = context.rsp;
}
#elif defined(CONFIG_ARCH_AARCH64)
void TaskControlBlock::save_context(uint64_t& rsp) noexcept {
    context.sp_el0 = rsp;
}

void TaskControlBlock::restore_context(uint64_t& rsp) noexcept {
    rsp = context.sp_el0;
}
#endif

/// @brief Clones the current task (fork).
/// Allocates a new TCB and kernel stack, copies parent's fd table, FPU state,
/// signal handlers, and process hierarchy. For user tasks, clones the page table
/// and copies user stack contents with COW semantics for code/data pages.
/// @param regs  Pointer to the saved register frame from the fork syscall.
/// @return  Pointer to the new child TCB, or nullptr on failure.
TaskControlBlock* TaskControlBlock::clone(uint64_t* regs) {
    auto* parent = Scheduler::current_task();
    if (!parent) return nullptr;

    auto* tcb = static_cast<TaskControlBlock*>(MemPool::alloc(sizeof(
        TaskControlBlock)));
    if (!tcb) return nullptr;
    memset(tcb, 0, sizeof(TaskControlBlock));

    tcb->magic = TCB_MAGIC;
    tcb->id = Scheduler::alloc_id();
    __builtin_memcpy(tcb->name, parent->name, CONFIG_TASK_NAME_LEN);
    tcb->parent_id = parent->id;
    tcb->state = TaskState::READY;
    tcb->priority = parent->priority;
    tcb->base_priority = parent->base_priority;
    tcb->period_ticks = parent->period_ticks;
    tcb->deadline_ticks = parent->deadline_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = parent->remaining_ticks;
    tcb->exit_code = 0;
    tcb->waiting_child_pid = 0;
    tcb->waiting_child_status = nullptr;
    tcb->user_data = parent->user_data;
    tcb->program_break = parent->program_break;
    tcb->program_break_start = parent->program_break_start;

    tcb->blocked_next = nullptr;
    tcb->blocked_prev = nullptr;

    // Process hierarchy: add child to parent
    tcb->first_child = nullptr;
    tcb->next_sibling = nullptr;
    tcb->prev_sibling = nullptr;
    tcb->num_children = 0;
    if (parent) {
        parent->add_child(tcb);
    }

    // Allocate per-task IPC objects (child gets fresh empty queues)
    auto* mq = static_cast<MessageQueue*>(MemPool::alloc(sizeof(MessageQueue)));
    if (mq) { mq->init(); mq->owner = tcb; tcb->msg_queue = mq;
        kernel::test::ResourceTracker::instance().track_msg_queue_add(); }
    else { tcb->msg_queue = nullptr; }

    auto* n = static_cast<sync::Notify*>(MemPool::alloc(sizeof(sync::Notify)));
    if (n) { n->init(); tcb->notify = n;
        kernel::test::ResourceTracker::instance().track_notify_add(); }
    else { tcb->notify = nullptr; }

    auto* eg = static_cast<sync::EventGroup*>(
        MemPool::alloc(sizeof(sync::EventGroup)));
    if (eg) { eg->init(); tcb->event_group = eg;
        kernel::test::ResourceTracker::instance().track_event_group_add(); }
    else { tcb->event_group = nullptr; }

    // Copy fd_table
    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        tcb->fd_table.fds[i] = parent->fd_table.fds[i];
    }

    // Copy cwd
    size_t cwd_len = 0;
    while (parent->cwd[cwd_len] && cwd_len < 255) ++cwd_len;
    for (size_t i = 0; i <= cwd_len; ++i) tcb->cwd[i] = parent->cwd[i];
    tcb->cwd_vnode = parent->cwd_vnode;

    // Buffer pool starts empty — buffers are NOT inherited on fork
    tcb->buf_list_head = -1;

#if defined(CONFIG_ARCH_X86_64)
    // Save parent's FPU state if it's currently in the registers
    if (__atomic_load_n(&fpu_owner, __ATOMIC_ACQUIRE) == parent) {
        arch::fxsave(parent->fpu_state);
    }
#else
    (void)parent;
#endif
    // Copy FPU state to child
    tcb->fpu_used = parent->fpu_used;
    if (parent->fpu_used) {
        __builtin_memcpy(tcb->fpu_state, parent->fpu_state, 512);
    }

    // Allocate and set up kernel stack
    size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t kstack_phys = PMM::alloc_contiguous(stack_pages);
    if (!kstack_phys) { ASSERT(errors::TaskError::TASK_ERR_STACK_ALLOC
        ); MemPool::free(tcb); return nullptr; }
    tcb->stack_phys_ = kstack_phys;

    bool is_user_task = (parent->page_table_ != 0);
    if (is_user_task) {
        uint64_t kstack_virt = arch::HHDM_OFFSET + kstack_phys;
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_virt);
        tcb->kernel_stack_top = kstack_virt + STACK_SIZE;
    } else {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_phys);
        tcb->kernel_stack_top = kstack_phys + STACK_SIZE;
    }

#if defined(CONFIG_ARCH_X86_64)
    // Build register frame on child's kernel stack
    // Layout matches isr_common push order (high to low):
    // SS, RSP, RFLAGS, CS, RIP, error, vector, r15..r8, rbp, rdi, rsi, rdx,
    // rcx, rbx, rax
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = regs[21]; // SS
    *--stack = regs[20]; // RSP
    *--stack = regs[19]; // RFLAGS
    *--stack = regs[18]; // CS
    *--stack = regs[17]; // RIP
    *--stack = regs[16]; // error_code
    *--stack = regs[15]; // vector
    *--stack = regs[14]; // r15
    *--stack = regs[13]; // r14
    *--stack = regs[12]; // r13
    *--stack = regs[11]; // r12
    *--stack = regs[10]; // r11
    *--stack = regs[9];  // r10
    *--stack = regs[8];  // r9
    *--stack = regs[7];  // r8
    *--stack = regs[6];  // rbp
    *--stack = regs[5];  // rdi
    *--stack = regs[4];  // rsi
    *--stack = regs[3];  // rdx
    *--stack = regs[2];  // rcx
    *--stack = regs[1];  // rbx
    *--stack = 0;        // rax = 0 (child return value)
    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_AARCH64)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    // aarch64 exception frame: padding, SPSR, ELR, SP_EL0, X0-X30
    *--stack = 0;                  // padding
    *--stack = 0;                  // padding
    *--stack = regs[19];           // SPSR_EL1 (from regs[19])
    *--stack = regs[17];           // ELR_EL1 (from regs[17])
    *--stack = regs[20];           // SP_EL0 (from regs[20])
    for (int i = 0; i < 31; ++i) *--stack = regs[i];
    tcb->context.sp_el0 = reinterpret_cast<uint64_t>(stack);
#elif defined(CONFIG_ARCH_RISCV64)
    // Copy trap frame from parent (regs matches syscall_entry.S save area layout:
    // [0..30]=X1-X31, [31]=SEPC, [32]=SSTATUS, [33]=SCAUSE, [34]=STVAL)
    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    stack -= 37;
    for (int i = 0; i < 37; ++i) stack[i] = regs[i];
    stack[9] = 0;   // X10/A0 = 0 (fork returns 0 in child)
    tcb->context.sp = reinterpret_cast<uint64_t>(stack);
#endif

    // For user tasks: clone page table and user stack
    if (is_user_task) {
        // Allocate a new PML4 page
        uint64_t new_pml4 = PMM::alloc_page();
        if (!new_pml4) { ASSERT(errors::TaskError::TASK_ERR_PML4_CLONE
            ); MemPool::free(tcb); return nullptr; }
        tcb->page_table_ = new_pml4;
        tcb->page_table_shared_ = true;

// Copy user entries (0-255) from parent's PML4 — shares PDPT/PD/PT pages
        // so the child inherits code/data/heap mappings without deep-copy.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* parent_pml4_virt = reinterpret_cast<uint64_t*>(arch::
            HHDM_OFFSET + (parent->page_table_ & ~0xFFFULL));
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* new_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            new_pml4 & ~0xFFFULL));
        for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
            new_virt[i] = parent_pml4_virt[i];
        }
        // Copy kernel entries (256-511) from the kernel PML4
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* kernel_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (
            VMM::get_kernel_pml4() & ~0xFFFULL));
        for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i) {
            new_virt[i] = kernel_virt[i];
        }

        // Create private page tables for the user stack region so that
        // mapping the child's stack below doesn't corrupt the parent's
        // mappings through shared PDPT/PD/PT pages.
        uint64_t stack_pdpt_phys = 0;
        {
            constexpr uint64_t PML4_SHIFT = 39;
            constexpr uint64_t PDPT_SHIFT = 30;
            constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
            size_t st_pml4_idx = (mem::STACK_VADDR >> PML4_SHIFT) & 0x1FF;
            size_t st_pdpt_idx = (mem::STACK_VADDR >> PDPT_SHIFT) & 0x1FF;
            if (new_virt[st_pml4_idx] & PAGE_PRESENT) {
                uint64_t old_pdpt_phys = new_virt[st_pml4_idx] & ~0xFFFULL;
                stack_pdpt_phys = PMM::alloc_user_page();
                // NOLINTNEXTLINE(performance-no-int-to-ptr)
                auto* old_pdpt = reinterpret_cast<uint64_t*>(arch::
                    HHDM_OFFSET + old_pdpt_phys);
                // NOLINTNEXTLINE(performance-no-int-to-ptr)
                auto* new_pdpt = reinterpret_cast<uint64_t*>(arch::
                    HHDM_OFFSET + stack_pdpt_phys);
                memcpy(new_pdpt, old_pdpt, arch::PAGE_SIZE);
                new_pdpt[st_pdpt_idx] = 0;
                new_virt[st_pml4_idx] = stack_pdpt_phys | (new_virt[st_pml4_idx
                    ] & 0xFFFULL);
            }
        }
        tcb->stack_pdpt_phys_ = stack_pdpt_phys;

        size_t ustack_pages = (parent->user_stack_size_ + 4095) / arch::
            PAGE_SIZE;
        uint64_t ustack_phys = PMM::alloc_user_contiguous(ustack_pages);
        if (!ustack_phys) {
            if (stack_pdpt_phys) PMM::free_page(stack_pdpt_phys);
            ASSERT(errors::TaskError::TASK_ERR_USTACK_ALLOC); MemPool::free(tcb
                ); return nullptr;
        }
        tcb->user_stack_ = ustack_phys;
        tcb->user_stack_size_ = parent->user_stack_size_;

// Guard page: leave first page unmapped, start mapping at +arch::PAGE_SIZE
        uint64_t user_stack_virt = mem::STACK_VADDR + arch::PAGE_SIZE;
        for (size_t i = 0; i < ustack_pages; ++i) {
            VMM::map_page_in_pml4(user_stack_virt + i * arch::PAGE_SIZE,
                                  ustack_phys + i * arch::PAGE_SIZE,
                                  true, new_pml4);
            // Copy physical page content via kernel mapping
            uint64_t src_virt = arch::
                HHDM_OFFSET + parent->user_stack_ + i * arch::PAGE_SIZE;
            uint64_t dst_virt = arch::HHDM_OFFSET + ustack_phys + i * arch::
                PAGE_SIZE;
            for (uint64_t j = 0; j < arch::PAGE_SIZE; ++j) {
                // NOLINTNEXTLINE(performance-no-int-to-ptr)
                reinterpret_cast<uint8_t*>(dst_virt)[j
                    // NOLINTNEXTLINE(performance-no-int-to-ptr)
                    ] = reinterpret_cast<const uint8_t*>(src_virt)[j];
            }
        }
    }

    return tcb;
}

void TaskControlBlock::add_child(TaskControlBlock* child) noexcept {
    if (!child) return;
    child->next_sibling = first_child;
    child->prev_sibling = nullptr;
    if (first_child) first_child->prev_sibling = child;
    first_child = child;
    child->parent_id = id;
    ++num_children;
}

void TaskControlBlock::remove_child(TaskControlBlock* child) noexcept {
    if (!child) return;
    bool found = false;
    for (auto* current = first_child; current; current = current->next_sibling
        ) {
        if (current == child) { found = true; break; }
    }
    if (!found) return;
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        first_child = child->next_sibling;
    }
    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    }
    child->parent_id = 0;
    child->prev_sibling = nullptr;
    child->next_sibling = nullptr;
    --num_children;
}

TaskControlBlock* TaskControlBlock::find_child(uint64_t pid) noexcept {
    for (auto* child = first_child; child; child = child->next_sibling) {
        if (child->id == pid) return child;
    }
    return nullptr;
}

/// @brief Frees the private stack PDPT and its PD/PT pages allocated during clone().
///        Only frees intermediate page-table pages (PD, PT), not leaf pages
///        (those are freed separately by the user_stack_ loop in cleanup()).
static void free_stack_pdpt(uint64_t pdpt_phys) noexcept {
    constexpr uint64_t PDPT_SHIFT = 30;
    constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
    constexpr uint64_t PAGE_HUGE = 1ULL << 7;
    size_t st_pdpt_idx = (mem::STACK_VADDR >> PDPT_SHIFT) & 0x1FF;

    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pdpt_phys);
    if (!(pdpt[st_pdpt_idx] & PAGE_PRESENT))
        return;

    uint64_t pd_phys = pdpt[st_pdpt_idx] & ~0xFFFULL;
    if (!PMM::is_user_page(pd_phys))
        return;

    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + pd_phys);
    for (int i = 0; i < 512; ++i) {
        if (!(pd[i] & PAGE_PRESENT)) continue;
        if (pd[i] & PAGE_HUGE
            ) continue; // leaf — already freed by user_stack_ loop
        uint64_t pt_phys = pd[i] & ~0xFFFULL;
        if (!PMM::is_user_page(pt_phys)) continue;
        PMM::free_page(pt_phys);
    }
    PMM::free_page(pd_phys);
    PMM::free_page(pdpt_phys);
}

/// @brief Releases all resources owned by the task.
/// Unlinks from parent's child list, detaches from message-queue blocked lists,
/// closes file descriptors, frees user/kernel stacks, tears down page tables,
/// destroys IPC objects, and notifies the daemon manager. After this call the
/// TCB must not be used except for MemPool::free().
void TaskControlBlock::cleanup() noexcept {
    magic = 0;
    // Remove self from parent's child list if we have a parent
    if (parent_id != 0) {
        auto* parent = Scheduler::find_task(parent_id);
        if (parent) {
            parent->remove_child(this);
        }
        parent_id = 0;
    }

    // Remove self from any message queue's blocked-senders list *before*
    // freeing any resources.  If we are blocked on another task's queue
    // (blocked_on_queue != nullptr) we must detach now — otherwise the
    // queue owner's cleanup() will walk a dangling pointer after we are
    // freed and poisoned.
    if (blocked_on_queue) {
        auto& q = *blocked_on_queue;
        if (q.blocked_senders_head == this) {
            q.blocked_senders_head = blocked_next;
            if (q.blocked_senders_tail == this)
                q.blocked_senders_tail = nullptr;
        } else {
            auto* prev = q.blocked_senders_head;
            while (prev && prev->blocked_next != this)
                prev = prev->blocked_next;
            if (prev) {
                prev->blocked_next = blocked_next;
                if (q.blocked_senders_tail == this)
                    q.blocked_senders_tail = prev;
            }
        }
        blocked_next = nullptr;
        blocked_on_queue = nullptr;
    }

    // Notify the daemon manager so registered daemon PIDs are reset to 0.
    // Must run before msg_queue is destroyed so blocked senders get fast-fail
    // instead of blocking on a zombie destination.
    kernel::daemon::notify_death(this->id);

    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        if (fd_table.fds[i].used) {
            auto* vn = fd_table.fds[i].vnode;
            if (vn && vn->refcount > 0) {
                --vn->refcount;
                if (vn->refcount == 0) {
                    if (vn->ops->close) vn->ops->close(*vn);
                }
            } else if (vn) {
                if (vn->ops->close) vn->ops->close(*vn);
            }
            kernel::test::ResourceTracker::instance().track_fd_remove();
            fd_table.fds[i].used = false;
            fd_table.fds[i].vnode = nullptr;
        }
    }

    if (user_stack_ && page_table_) {
        size_t pages = (user_stack_size_ + 4095) / arch::PAGE_SIZE;
        for (size_t i = 0; i < pages; ++i) {
            PMM::free_page(user_stack_ + i * arch::PAGE_SIZE);
        }
        user_stack_ = 0;
    }

    if (stack_phys_) {
        size_t pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
        for (size_t i = 0; i < pages; ++i) {
            PMM::free_page(stack_phys_ + i * arch::PAGE_SIZE);
        }
        stack_phys_ = 0;
        kernel_stack = nullptr;
        kernel_stack_top = 0;
    }

    if (page_table_) {
// Free any zero-copy buffers owned by this task before tearing down page tables
        BufferPool::unmap_all(*this);

        if (!page_table_shared_) {
            VMM::free_user_pages(page_table_);
        }
        if (stack_pdpt_phys_) {
            free_stack_pdpt(stack_pdpt_phys_);
            stack_pdpt_phys_ = 0;
        }
        PMM::free_page(page_table_);
        page_table_ = 0;
    }

    if (msg_queue) {
        msg_queue->~MessageQueue();
        kernel::test::ResourceTracker::instance().track_msg_queue_remove();
        MemPool::free(msg_queue);
        msg_queue = nullptr;
    }
    if (notify) {
        notify->~Notify();
        kernel::test::ResourceTracker::instance().track_notify_remove();
        MemPool::free(notify);
        notify = nullptr;
    }
    if (event_group) {
        event_group->~EventGroup();
        kernel::test::ResourceTracker::instance().track_event_group_remove();
        MemPool::free(event_group);
        event_group = nullptr;
    }
    if (sporadic_server) {
        Scheduler::dec_sporadic_count();
        MemPool::free(sporadic_server);
        sporadic_server = nullptr;
    }
    kernel::test::ResourceTracker::instance().track_task_remove();
}
 
} // namespace kernel
 
// --- Error-returning overloads ---
namespace kernel {
 
using namespace errors;
 
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
TaskError TaskControlBlock::create_err(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks,
    TaskControlBlock*& out_tcb)
{
    out_tcb = create(entry, priority, period_ticks);
    return out_tcb ? TASK_ERR_OK : TASK_ERR_OOM;
}
 
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
TaskError TaskControlBlock::create_user_err(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks,
    size_t user_stack_size,
    TaskControlBlock*& out_tcb)
{
    out_tcb = create_user(entry, priority, period_ticks, user_stack_size);
    return out_tcb ? TASK_ERR_OK : TASK_ERR_OOM;
}
 
TaskError TaskControlBlock::clone_err(uint64_t* regs, TaskControlBlock*& out_tcb) {
    out_tcb = clone(regs);
    return out_tcb ? TASK_ERR_OK : TASK_ERR_OOM;
}
 
} // namespace kernel
