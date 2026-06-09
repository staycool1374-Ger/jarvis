#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <assert.hpp>
#include <string.hpp>
#include <constants.hpp>

namespace kernel {

static uint64_t next_task_id = 1;

static void init_task_common(TaskControlBlock* tcb) {
    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        tcb->fd_table.fds[i].used = false;
        tcb->fd_table.fds[i].vnode = nullptr;
        tcb->fd_table.fds[i].offset = 0;
        tcb->fd_table.fds[i].flags = 0;
    }
    tcb->cwd[0] = '/';
    tcb->cwd[1] = '\0';
    tcb->cwd_vnode = vfs::get_root_vnode();

    // Initialize IPC message queue
    auto* mq = new MessageQueue{};
    if (mq) {
        mq->init();
        mq->owner = tcb;
        tcb->msg_queue = mq;
    } else {
        tcb->msg_queue = nullptr;
    }
    tcb->blocked_next = nullptr;
    tcb->blocked_prev = nullptr;

    // Per-task notification object
    auto* n = new sync::Notify{};
    if (n) {
        n->init();
        tcb->notify = n;
    } else {
        tcb->notify = nullptr;
    }

    // Per-task event-group object
    auto* eg = new sync::EventGroup{};
    if (eg) {
        eg->init();
        tcb->event_group = eg;
    } else {
        tcb->event_group = nullptr;
    }

    // Process hierarchy initialization
    tcb->first_child = nullptr;
    tcb->next_sibling = nullptr;
    tcb->prev_sibling = nullptr;
    tcb->num_children = 0;
}

TaskControlBlock* TaskControlBlock::create(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks)
{
    auto* tcb = new TaskControlBlock{};
    ASSERT(tcb != nullptr);

    tcb->id = next_task_id++;
    tcb->state = TaskState::READY;
    tcb->priority = priority;
    tcb->base_priority = priority;
    tcb->period_ticks = period_ticks;
    tcb->deadline_ticks = period_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = period_ticks;
    init_task_common(tcb);

    size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t stack_phys = PMM::alloc_contiguous(stack_pages);
    ASSERT(stack_phys != 0);

    tcb->stack_phys_ = stack_phys;
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(stack_phys);
    tcb->kernel_stack_top = stack_phys + STACK_SIZE;

    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);

    *--stack = arch::SEG_KERNEL_DATA;
    *--stack = tcb->kernel_stack_top;

    *--stack = arch::RFLAGS_DEFAULT;
    *--stack = arch::SEG_KERNEL_CODE;
    *--stack = reinterpret_cast<uint64_t>(entry);

    *--stack = 0;
    *--stack = 0;

    for (int i = 0; i < 15; ++i) *--stack = 0;

    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);

    return tcb;
}

TaskControlBlock* TaskControlBlock::create_user(
    void (*entry)(),
    uint64_t priority,
    uint64_t period_ticks,
    size_t user_stack_size)
{
    auto* tcb = new TaskControlBlock{};
    if (!tcb) return nullptr;

    tcb->id = next_task_id++;
    tcb->state = TaskState::READY;
    tcb->priority = priority;
    tcb->base_priority = priority;
    tcb->period_ticks = period_ticks;
    tcb->deadline_ticks = period_ticks;
    tcb->executed_ticks = 0;
    tcb->remaining_ticks = period_ticks;
    init_task_common(tcb);

    size_t kernel_stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t kstack_phys = PMM::alloc_contiguous(kernel_stack_pages);
    if (!kstack_phys) { delete tcb; return nullptr; }
    tcb->stack_phys_ = kstack_phys;

    uint64_t kstack_virt = arch::HHDM_OFFSET + kstack_phys;
    tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_virt);
    tcb->kernel_stack_top = kstack_virt + STACK_SIZE;

    size_t user_stack_pages = (user_stack_size + 4095) / arch::PAGE_SIZE;
    uint64_t ustack_phys = PMM::alloc_user_contiguous(user_stack_pages);
    if (!ustack_phys) { delete tcb; return nullptr; }

    uint64_t pml4 = VMM::clone_kernel_pml4();
    if (!pml4) { delete tcb; return nullptr; }
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

    uint64_t* stack = reinterpret_cast<uint64_t*>(tcb->kernel_stack_top);
    *--stack = arch::SEG_USER_DATA;
    *--stack = user_rsp;
    *--stack = arch::RFLAGS_DEFAULT;
    *--stack = arch::SEG_USER_CODE;
    *--stack = reinterpret_cast<uint64_t>(entry);
    *--stack = 0;
    *--stack = 0;
    for (int i = 0; i < 15; ++i) *--stack = 0;
    tcb->context.rsp = reinterpret_cast<uint64_t>(stack);

    return tcb;
}

void TaskControlBlock::save_context(uint64_t* rsp) noexcept {
    context.rsp = *rsp;
}

void TaskControlBlock::restore_context(uint64_t* rsp) noexcept {
    *rsp = context.rsp;
}

TaskControlBlock* TaskControlBlock::clone(uint64_t* regs) {
    auto* parent = Scheduler::current_task();
    if (!parent) return nullptr;

    auto* tcb = new TaskControlBlock{};
    if (!tcb) return nullptr;

    tcb->id = next_task_id++;
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
    auto* mq = new MessageQueue{};
    if (mq) { mq->init(); mq->owner = tcb; tcb->msg_queue = mq; } else { tcb->msg_queue = nullptr; }

    auto* n = new sync::Notify{};
    if (n) { n->init(); tcb->notify = n; } else { tcb->notify = nullptr; }

    auto* eg = new sync::EventGroup{};
    if (eg) { eg->init(); tcb->event_group = eg; } else { tcb->event_group = nullptr; }

    // Copy fd_table
    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        tcb->fd_table.fds[i] = parent->fd_table.fds[i];
    }

    // Copy cwd
    size_t cwd_len = 0;
    while (parent->cwd[cwd_len] && cwd_len < 255) ++cwd_len;
    for (size_t i = 0; i <= cwd_len; ++i) tcb->cwd[i] = parent->cwd[i];
    tcb->cwd_vnode = parent->cwd_vnode;

    // Allocate and set up kernel stack
    size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t kstack_phys = PMM::alloc_contiguous(stack_pages);
    if (!kstack_phys) { delete tcb; return nullptr; }
    tcb->stack_phys_ = kstack_phys;

    bool is_user_task = (parent->page_table_ != 0);
    if (is_user_task) {
        uint64_t kstack_virt = arch::HHDM_OFFSET + kstack_phys;
        tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_virt);
        tcb->kernel_stack_top = kstack_virt + STACK_SIZE;
    } else {
        tcb->kernel_stack = reinterpret_cast<uint8_t*>(kstack_phys);
        tcb->kernel_stack_top = kstack_phys + STACK_SIZE;
    }

    // Build register frame on child's kernel stack
    // Layout matches isr_common push order (high to low):
    //   SS, RSP, RFLAGS, CS, RIP, error, vector, r15..r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
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

    // For user tasks: clone page table and user stack
    if (is_user_task) {
        // Allocate a new PML4 page
        uint64_t new_pml4 = PMM::alloc_page();
        if (!new_pml4) { delete tcb; return nullptr; }
        tcb->page_table_ = new_pml4;
        tcb->page_table_shared_ = true;

        // Copy user entries (0-255) from parent's PML4 — shares PDPT/PD/PT pages
        // so the child inherits code/data/heap mappings without deep-copy.
        auto* parent_pml4_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (parent->page_table_ & ~0xFFFULL));
        auto* new_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (new_pml4 & ~0xFFFULL));
        for (size_t i = 0; i < arch::PML4_USER_COUNT; ++i) {
            new_virt[i] = parent_pml4_virt[i];
        }
        // Copy kernel entries (256-511) from the kernel PML4
        auto* kernel_virt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (VMM::get_kernel_pml4() & ~0xFFFULL));
        for (size_t i = arch::PML4_KERNEL_START; i < arch::PML4_ENTRIES; ++i) {
            new_virt[i] = kernel_virt[i];
        }

        // Create private page tables for the user stack region so that
        // mapping the child's stack below doesn't corrupt the parent's
        // mappings through shared PDPT/PD/PT pages.
        {
            constexpr uint64_t PML4_SHIFT = 39;
            constexpr uint64_t PDPT_SHIFT = 30;
            constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
            size_t st_pml4_idx = (mem::STACK_VADDR >> PML4_SHIFT) & 0x1FF;
            size_t st_pdpt_idx = (mem::STACK_VADDR >> PDPT_SHIFT) & 0x1FF;
            if (new_virt[st_pml4_idx] & PAGE_PRESENT) {
                uint64_t old_pdpt_phys = new_virt[st_pml4_idx] & ~0xFFFULL;
                uint64_t new_pdpt_phys = PMM::alloc_page_table();
                auto* old_pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + old_pdpt_phys);
                auto* new_pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + new_pdpt_phys);
                memcpy(new_pdpt, old_pdpt, arch::PAGE_SIZE);
                new_pdpt[st_pdpt_idx] = 0;
                new_virt[st_pml4_idx] = new_pdpt_phys | (new_virt[st_pml4_idx] & 0xFFFULL);
            }
        }

        size_t ustack_pages = (parent->user_stack_size_ + 4095) / arch::PAGE_SIZE;
        uint64_t ustack_phys = PMM::alloc_user_contiguous(ustack_pages);
        if (!ustack_phys) { delete tcb; return nullptr; }
        tcb->user_stack_ = ustack_phys;
        tcb->user_stack_size_ = parent->user_stack_size_;

        // Guard page: leave first page unmapped, start mapping at +arch::PAGE_SIZE
        uint64_t user_stack_virt = mem::STACK_VADDR + arch::PAGE_SIZE;
        for (size_t i = 0; i < ustack_pages; ++i) {
            VMM::map_page_in_pml4(user_stack_virt + i * arch::PAGE_SIZE,
                                  ustack_phys + i * arch::PAGE_SIZE,
                                  true, new_pml4);
            // Copy physical page content via kernel mapping
            uint64_t src_virt = arch::HHDM_OFFSET + parent->user_stack_ + i * arch::PAGE_SIZE;
            uint64_t dst_virt = arch::HHDM_OFFSET + ustack_phys + i * arch::PAGE_SIZE;
            for (uint64_t j = 0; j < arch::PAGE_SIZE; ++j) {
                reinterpret_cast<uint8_t*>(dst_virt)[j] = reinterpret_cast<const uint8_t*>(src_virt)[j];
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
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        first_child = child->next_sibling;
    }
    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    }
    child->parent_id = 0;
    if (num_children > 0) --num_children;
}

TaskControlBlock* TaskControlBlock::find_child(uint64_t pid) noexcept {
    for (auto* child = first_child; child; child = child->next_sibling) {
        if (child->id == pid) return child;
    }
    return nullptr;
}

void TaskControlBlock::cleanup() noexcept {
    for (size_t i = 0; i < vfs::MAX_FDS; ++i) {
        if (fd_table.fds[i].used) {
            auto* vn = fd_table.fds[i].vnode;
            if (vn && vn->refcount > 0) {
                --vn->refcount;
                if (vn->refcount == 0) {
                    if (vn->ops->close) vn->ops->close(vn);
                }
            } else if (vn) {
                if (vn->ops->close) vn->ops->close(vn);
            }
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
        if (!page_table_shared_) {
            VMM::free_user_pages(page_table_);
        }
        PMM::free_page(page_table_);
        page_table_ = 0;
    }

    if (msg_queue) { delete msg_queue; msg_queue = nullptr; }
    if (notify) { delete notify; notify = nullptr; }
    if (event_group) { delete event_group; event_group = nullptr; }
}

} // namespace kernel
