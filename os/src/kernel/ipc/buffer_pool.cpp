#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <assert.hpp>
#include <string.hpp>

namespace kernel {

BufferPool::Entry BufferPool::entries[MAX_BUFFERS];
int32_t BufferPool::free_head_ = -1;
uint32_t BufferPool::next_cookie_ = 1;

static void clear_pte_in_pml4(uint64_t virt_addr, uint64_t pml4_phys) {
    constexpr uint64_t PML4_SHIFT = 39;
    constexpr uint64_t PDPT_SHIFT = 30;
    constexpr uint64_t PD_SHIFT = 21;
    constexpr uint64_t PT_SHIFT = 12;
    constexpr uint64_t PAGE_PRESENT = 1ULL << 0;

    auto* pml4 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4_phys & ~0xFFFULL));
    size_t pml4_idx = (virt_addr >> PML4_SHIFT) & 0x1FF;
    size_t pdpt_idx = (virt_addr >> PDPT_SHIFT) & 0x1FF;
    size_t pd_idx   = (virt_addr >> PD_SHIFT) & 0x1FF;
    size_t pt_idx   = (virt_addr >> PT_SHIFT) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return;
    auto* pdpt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pml4[pml4_idx] & ~0xFFFULL));
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;
    auto* pd = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pdpt[pdpt_idx] & ~0xFFFULL));
    if (pd[pd_idx] & PAGE_PRESENT) {
        if (pd[pd_idx] & (1ULL << 7)) { pd[pd_idx] = 0; return; } // huge page
        auto* pt = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + (pd[pd_idx] & ~0xFFFULL));
        pt[pt_idx] = 0;
    }
}

static void list_insert(TaskControlBlock* task, int32_t idx) {
    int32_t old_head = task->buf_list_head;
    BufferPool::entries[idx].list_prev = -1;
    BufferPool::entries[idx].list_next = old_head;
    if (old_head != -1)
        BufferPool::entries[old_head].list_prev = idx;
    task->buf_list_head = idx;
}

static void list_remove(TaskControlBlock* task, int32_t idx) {
    int32_t prev = BufferPool::entries[idx].list_prev;
    int32_t next = BufferPool::entries[idx].list_next;
    if (prev != -1) BufferPool::entries[prev].list_next = next;
    else task->buf_list_head = next;
    if (next != -1) BufferPool::entries[next].list_prev = prev;
    BufferPool::entries[idx].list_prev = -1;
    BufferPool::entries[idx].list_next = -1;
}

void BufferPool::init() {
    free_head_ = -1;
    next_cookie_ = 1;
    for (size_t i = 0; i < MAX_BUFFERS; ++i) {
        entries[i].phys_addr = 0;
        entries[i].generation = 0;
        entries[i].refcount = 0;
        entries[i].owner_task = 0;
        entries[i].mapped_va = 0;
        entries[i].list_prev = -1;
        entries[i].list_next = -1;
        free_entry(static_cast<int32_t>(i));
    }
}

int32_t BufferPool::alloc_entry() {
    if (free_head_ == -1) return -1;
    int32_t idx = free_head_;
    free_head_ = static_cast<int32_t>(entries[idx].list_next);
    ENSURE(entries[idx].phys_addr == 0);
    entries[idx].list_next = -1;
    return idx;
}

void BufferPool::free_entry(int32_t idx) {
    ENSURE(idx >= 0 && static_cast<size_t>(idx) < MAX_BUFFERS);
    entries[idx].phys_addr = 0;
    entries[idx].refcount = 0;
    entries[idx].owner_task = 0;
    entries[idx].mapped_va = 0;
    entries[idx].list_prev = -1;
    entries[idx].list_next = free_head_;
    free_head_ = idx;
}

int32_t BufferPool::validate(uint64_t handle) {
    if (handle == 0) return -1;
    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    uint32_t gen = static_cast<uint32_t>(handle >> 32);
    if (idx >= MAX_BUFFERS) return -1;
    if (entries[idx].phys_addr == 0) return -1;
    if (entries[idx].generation != gen) return -1;
    return static_cast<int32_t>(idx);
}

uint64_t BufferPool::alloc(TaskControlBlock* task, uint64_t va) {
    if (!task || !task->page_table_) return 0;

    if (va >= USER_SPACE_LIMIT) return 0;

    int32_t idx = alloc_entry();
    if (idx < 0) return 0;

    uint64_t phys = PMM::alloc_page();
    if (!phys) {
        free_entry(idx);
        return 0;
    }

    VMM::map_page_in_pml4(va, phys, true, task->page_table_);

    entries[idx].phys_addr = phys;
    entries[idx].generation = next_cookie_++;
    entries[idx].owner_task = static_cast<uint32_t>(task->id);
    entries[idx].mapped_va = va;

    list_insert(task, idx);

    return (static_cast<uint64_t>(entries[idx].generation) << 32) | static_cast<uint64_t>(idx);
}

bool BufferPool::free(TaskControlBlock* task, uint64_t handle) {
    int32_t idx = validate(handle);
    if (idx < 0) return false;
    if (entries[idx].owner_task != static_cast<uint32_t>(task->id))
        return false;

    if (entries[idx].mapped_va) {
        clear_pte_in_pml4(entries[idx].mapped_va, task->page_table_);
        entries[idx].mapped_va = 0;
    }

    list_remove(task, idx);
    PMM::free_page(entries[idx].phys_addr);
    free_entry(idx);
    return true;
}

bool BufferPool::map(TaskControlBlock* task, uint64_t handle, uint64_t va) {
    int32_t idx = validate(handle);
    if (idx < 0) return false;
    if (va >= USER_SPACE_LIMIT) return false;

    VMM::map_page_in_pml4(va, entries[idx].phys_addr, true, task->page_table_);

    entries[idx].owner_task = static_cast<uint32_t>(task->id);
    entries[idx].mapped_va = va;

    list_insert(task, idx);
    return true;
}

bool BufferPool::unmap(TaskControlBlock* task, uint64_t handle) {
    int32_t idx = validate(handle);
    if (idx < 0) return false;
    if (entries[idx].owner_task != static_cast<uint32_t>(task->id))
        return false;
    if (!entries[idx].mapped_va) return false;

    clear_pte_in_pml4(entries[idx].mapped_va, task->page_table_);
    entries[idx].mapped_va = 0;

    list_remove(task, idx);
    return true;
}

bool BufferPool::transfer(uint64_t handle, TaskControlBlock* from, TaskControlBlock* to) {
    int32_t idx = validate(handle);
    if (idx < 0) return false;
    if (entries[idx].owner_task != static_cast<uint32_t>(from->id))
        return false;

    if (entries[idx].mapped_va) {
        clear_pte_in_pml4(entries[idx].mapped_va, from->page_table_);
        entries[idx].mapped_va = 0;
    }

    list_remove(from, idx);

    entries[idx].owner_task = static_cast<uint32_t>(to->id);

    return true;
}

void BufferPool::unmap_all(TaskControlBlock* task) {
    int32_t idx = task->buf_list_head;
    while (idx != -1) {
        int32_t next = entries[idx].list_next;
        ENSURE(entries[idx].owner_task == static_cast<uint32_t>(task->id));

        if (entries[idx].mapped_va) {
            clear_pte_in_pml4(entries[idx].mapped_va, task->page_table_);
            entries[idx].mapped_va = 0;
        }

        PMM::free_page(entries[idx].phys_addr);
        free_entry(idx);
        idx = next;
    }
    task->buf_list_head = -1;
}

} // namespace kernel
