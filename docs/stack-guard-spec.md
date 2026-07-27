# Stack Guard Page — Kernel Stack Overflow Detection

- **Status:** Draft
- **Target:** v0.3.5 (Stack Allocation — Fixed, Guarded, No Growth)
- **Design Doc:** This file

## 1. Motivation

Currently kernel stacks are allocated as contiguous physical pages mapped 1:1
via the HHDM (`HHDM_OFFSET + phys`).  A kernel stack overflow silently
corrupts adjacent physical pages (the next task's stack, page table pages, or
pool metadata).  The corruption is not detected until the system crashes
miles away from the actual overflow, making debugging extremely difficult.

### Design Goals

- Detect kernel stack overflow at the **instruction that causes it** (#PF on
  guard page access), not minutes later via symptom corruption.
- Invoke a configurable **overflow hook** (weak symbol) for diagnostic dump
  or fault recovery.
- Zero runtime overhead in the non-overflow case (no canary checks on context
  switch).
- Compatible with HHDM: kernel stacks currently live in the direct-physical-
  map window, which cannot provide unmapped guard pages.  A separate virtual
  address range is required.

## 2. Kernel Stack Virtual Window

### 2.1 Problem

Kernel stacks currently use the HHDM direct map:
```
kernel_stack = (uint8_t*)(HHDM_OFFSET + stack_phys)
kernel_stack_top = kernel_stack + STACK_SIZE
```

The HHDM maps **all physical memory 1:1** in every page table.  There is no
way to leave a single page unmapped within this window — every physical page
is always present.  A guard page adjacent to the stack would be reachable via
the HHDM just like the stack itself.

### 2.2 Solution: Dedicated Kernel Stack VA Range

Reserve a large contiguous virtual-address range in the kernel half of the
address space for kernel stacks.  Each task receives a fixed-size slot
comprising the stack pages PLUS an unmapped guard page at the bottom.

```
KERNEL_STACK_BASE = 0xFFFF900000000000ULL   (example — pick a gap in kernel space)
SLOT_SIZE         = STACK_SLOT_SIZE           (e.g. 68 KiB = 64 KiB stack + 4 KiB guard)
```

Mapping for task at index `i`:

```
slot_base = KERNEL_STACK_BASE + i * SLOT_SIZE
guard     = slot_base                         (unmapped — 4 KiB)
stack     = slot_base + PAGE_SIZE             (mapped — 64 KiB)
top       = slot_base + SLOT_SIZE
```

**Why this works:** The kernel VA range is mapped in the kernel PML4, which
is cloned into every user PML4 at boot.  A single `VMM::map_page()` call
per stack (against `kernel_pml4_`) makes the stack visible in ALL address
spaces — exactly the same way HHDM mappings are global.  The guard page is
simply never mapped, so any access (read or write) to `[slot_base,
slot_base + PAGE_SIZE)` triggers a #PF.

### 2.3 Configuration

```c
/// Base virtual address of the kernel-stack window.  Must be a large
/// aligned gap in kernel space (above HHDM, below device MMIO).
#ifndef CONFIG_KSTACK_WINDOW_BASE
#define CONFIG_KSTACK_WINDOW_BASE 0xFFFF900000000000ULL
#endif

/// Total virtual address space reserved for kernel stacks.  Upper bound:
/// CONFIG_MAX_TASKS × (CONFIG_STACK_SIZE + 4096) ≈ 64 × 68 KiB = 4.25 MiB.
/// Rounded up to a 2 MiB-aligned region to fit in a single PD entry.
/// Default: 8 MiB (enough for 64 max-size stacks with headroom).
#ifndef CONFIG_KSTACK_WINDOW_SIZE
#define CONFIG_KSTACK_WINDOW_SIZE 0x800000ULL    // 8 MiB
#endif
```

Constraint: `CONFIG_KSTACK_WINDOW_BASE + CONFIG_KSTACK_WINDOW_SIZE` must
not overlap other kernel regions (HHDM, device MMIO, LAPIC, etc.).

### 2.4 Page Table Changes

In `init_kernel_pml4()` (or equivalent), after the HHDM is set up, identity-
map the kernel-stack window:

```cpp
// Map all PML4 entries for the window as not-present initially.
// Individual stack slots are populated on task creation.
for (uint64_t i = 0; i < num_stack_slots; ++i) {
    uint64_t slot = KERNEL_STACK_BASE + i * SLOT_SIZE;
    // PDPT/PD/PT pages allocated on demand by VMM::map_page.
    // The guard page at slot+0 is never mapped.
    // Stack pages slot+PAGE_SIZE .. slot+SLOT_SIZE-PAGE_SIZE are mapped
    // as kernel read/write.
}
```

Since this is in the kernel PML4, all user PML4s (cloned from kernel PML4
at `clone_kernel_pml4()`) automatically see the same mapping.

## 3. Stack Allocation Changes

### 3.1 `TaskControlBlock::create()` — current allocation

Current (lines 292-314 of `task.cpp`):
```cpp
size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
uint64_t stack_phys = PMM::alloc_contiguous(stack_pages);
tcb->stack_phys_ = stack_phys;
uint64_t stack_virt = arch::HHDM_OFFSET + stack_phys;
tcb->kernel_stack = reinterpret_cast<uint8_t *>(stack_virt);
tcb->kernel_stack_top = stack_virt + STACK_SIZE;
```

### 3.2 New allocation

```cpp
size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
uint64_t stack_phys = PMM::alloc_contiguous(stack_pages);
tcb->stack_phys_ = stack_phys;

// Allocate slot from the kernel-stack window.
uint64_t slot_va = alloc_kstack_slot();   // linear allocator or bitmap
// Guard page at slot_va is left unmapped.
// Map stack pages starting at slot_va + PAGE_SIZE:
for (size_t i = 0; i < stack_pages; ++i) {
    VMM::map_page(slot_va + PAGE_SIZE + i * arch::PAGE_SIZE,
                  stack_phys + i * arch::PAGE_SIZE,
                  false /* user=false, kernel only */);
}

tcb->kernel_stack = reinterpret_cast<uint8_t *>(slot_va + PAGE_SIZE);
tcb->kernel_stack_top = slot_va + SLOT_SIZE;    // = base of next slot
```

### 3.3 `free_kstack_slot()`

On cleanup, unmap the stack pages and return the slot to the allocator:

```cpp
size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
uint64_t slot_va = reinterpret_cast<uint64_t>(tcb->kernel_stack) - PAGE_SIZE;
for (size_t i = 0; i < stack_pages; ++i)
    VMM::unmap_page(slot_va + PAGE_SIZE + i * arch::PAGE_SIZE);
// Guard page was never mapped, nothing to unmap.
free_kstack_slot(slot_va);
```

### 3.4 Variable-size allocator (bump + free list)

Different tasks need different kernel stack sizes:
- Idle task: minimal stack (e.g. 4 KiB + guard)
- Daemons (vfsd, iocd): moderate stack (e.g. 16 KiB + guard)
- RT periodic tasks: standard stack (e.g. 32 KiB + guard)
- Shell / complex ELF programs: large stack (e.g. 64 KiB + guard)
- `runelf` programs: explicit size from ELF header or manifest

The allocator uses a **bump pointer** for new allocations and a
**singly-linked free list** for reclaimed slots.  Each free-list entry
records the slot's VA and size so a future allocation of matching or
smaller size can reuse it.

```cpp
struct KStackFreeEntry {
    uint64_t           va;
    uint64_t           size;   // includes guard page
    KStackFreeEntry   *next;
};

static KStackFreeEntry *s_kstack_free_list = nullptr;
static uint64_t        s_kstack_bump     = KERNEL_STACK_BASE;
```

**Allocation:**

```cpp
uint64_t alloc_kstack_slot(uint64_t stack_size) {
    uint64_t slot_size = stack_size + PAGE_SIZE;   // + guard page

    // 1. Try free list (first-fit).
    KStackFreeEntry **pp = &s_kstack_free_list;
    while (*pp) {
        if ((*pp)->size >= slot_size) {
            uint64_t va = (*pp)->va;
            // Exact match: remove from list.
            // Larger than needed: can split (optional, deferred).
            KStackFreeEntry *entry = *pp;
            *pp = entry->next;
            return va;
        }
        pp = &(*pp)->next;
    }

    // 2. Bump allocate.
    uint64_t va = s_kstack_bump;
    if (va + slot_size > KERNEL_STACK_BASE + KERNEL_STACK_WINDOW_SIZE)
        panic("kernel-stack window exhausted");
    s_kstack_bump += slot_size;
    return va;
}
```

**Free:**

```cpp
void free_kstack_slot(uint64_t va, uint64_t size) {
    // Push onto free list.
    auto *entry = StaticAlloc<KStackFreeEntry>::alloc();
    if (!entry) return;   // free list OOM — slot leak (rare, bounded)
    entry->va   = va;
    entry->size = size;
    entry->next = s_kstack_free_list;
    s_kstack_free_list = entry;
}
```

The `KStackFreeEntry` nodes are allocated from a small static pool (max
`CONFIG_MAX_TASKS` nodes, each 24 bytes = ~1.5 KiB total).

**Size determination:**

```cpp
// In TaskControlBlock::create():
uint64_t stack_size = stack_size_for_priority(priority);
uint64_t slot_va = alloc_kstack_slot(stack_size);
tcb->kstack_slot_va_ = slot_va;            // new TCB field
tcb->kstack_slot_size_ = stack_size + PAGE_SIZE;

// In cleanup():
free_kstack_slot(tcb->kstack_slot_va_, tcb->kstack_slot_size_);
```

The size-for-priority mapping is a configurable array:

```c
/// Kernel stack sizes per priority tier (bytes).
/// Indexed by priority / CONFIG_STACK_TIER_SHIFT.
/// Default: idle(4K), daemon(16K), RT(32K), normal(64K).
#ifndef CONFIG_STACK_SIZE_TABLE
#define CONFIG_STACK_SIZE_TABLE \
    { 4096, 4096, 16384, 16384, 32768, 32768, 65536, 65536 }
#endif
```

For `runelf`, the ELF loader can override the size by passing an explicit
`stack_size` parameter to `create_user()`.

## 4. Page Fault Handler Changes

### 4.1 Kernel-mode #PF detection

In `handle_interrupt_c()` (`kernel.cpp` line ~1356), after the user-fault
path and before the fatal-panic path, add a check for guard-page access:

```cpp
if (vector == 14) {  // #PF
    uint64_t cr2 = read_cr2();
    // Kernel-mode page fault — check for stack guard access first.
    if (!from_user && cr2 >= KERNEL_STACK_BASE &&
        cr2 <  KERNEL_STACK_BASE + CONFIG_MAX_TASKS * CONFIG_KSTACK_SLOT_SIZE) {
        // Fault is within the kernel-stack window.  Find the owning task.
        uint64_t slot_base = cr2 & ~(CONFIG_KSTACK_SLOT_SIZE - 1);
        // slot_base is the guard page of some task.
        auto *t = find_task_by_kstack_slot(slot_base);
        if (t) {
            Logger::fatal("STACK OVERFLOW: task '%s' (ID=%u) overflowed kernel stack",
                          t->name, t->id);
            // Invoke weak hook if configured.
#if CONFIG_STACK_OVERFLOW_HOOK
            extern void stack_overflow_hook(void *task) __attribute__((weak));
            if (stack_overflow_hook)
                stack_overflow_hook(t);
#endif
            // Hook returns or is null — panic.
            dump_regs(regs);
            panic("kernel stack overflow");
        }
        // Fall through to normal panic if task not found.
    }
}
```

### 4.2 `find_task_by_kstack_slot()`

```cpp
TaskControlBlock *Scheduler::find_task_by_kstack_slot(uint64_t slot_va) {
    for (uint64_t i = 0; i < task_count(); ++i) {
        auto *t = task_at(i);
        if (t && t->magic == TaskControlBlock::TCB_MAGIC &&
            reinterpret_cast<uint64_t>(t->kernel_stack) - PAGE_SIZE == slot_va)
            return t;
    }
    return nullptr;
}
```

## 5. Task Cleanup Changes

### 5.1 `cleanup()` — free slot + unmap

In `TaskControlBlock::cleanup()` (`task.cpp` line ~984), replace the current
PMM free for the kernel stack:

```cpp
if (stack_phys_) {
    size_t pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
    uint64_t slot_va = reinterpret_cast<uint64_t>(kernel_stack) - PAGE_SIZE;

    // Unmap stack pages from the kernel-stack window.
    for (size_t i = 0; i < pages; ++i)
        VMM::unmap_page(slot_va + PAGE_SIZE + i * arch::PAGE_SIZE);

    // Poison physical pages via HHDM alias before returning to PMM.
    // PMM::free_page only clears the bitmap and does NOT clear content.
    // Without poisoning, a reallocated task could read the previous task's
    // stack data (register values, file paths, kernel pointers).
    uint64_t hhdm_base = arch::HHDM_OFFSET + stack_phys_;
    __builtin_memset(reinterpret_cast<void *>(hhdm_base), 0xDD,
                     pages * arch::PAGE_SIZE);

    // Free physical pages.
    for (size_t i = 0; i < pages; ++i)
        PMM::free_page(stack_phys_ + i * arch::PAGE_SIZE);

#if CONFIG_MEMORY_BUDGET
    Scheduler::release_memory_pages(pages);
#endif

    // Return slot to allocator (free list).
    free_kstack_slot(slot_va, tcb->kstack_slot_size_);

    stack_phys_ = 0;
    kernel_stack = nullptr;
    kernel_stack_top = 0;
}
```

### 5.2 `clone()` — fork stack

In `TaskControlBlock::clone()` (`task.cpp` line ~628), the forked task's
kernel stack must be in a separate slot (different from the parent's):

```cpp
// Allocate new physical stack + map into a new slot (same as create).
// Clone inherits the parent's stack size.
size_t stack_pages = (STACK_SIZE + 4095) / arch::PAGE_SIZE;
uint64_t kstack_phys = PMM::alloc_contiguous(stack_pages);
tcb->stack_phys_ = kstack_phys;

uint64_t slot_va = alloc_kstack_slot(tcb->kstack_slot_size_);
for (size_t i = 0; i < stack_pages; ++i) {
    VMM::map_page(slot_va + PAGE_SIZE + i * arch::PAGE_SIZE,
                  kstack_phys + i * arch::PAGE_SIZE, false);
}
tcb->kernel_stack = reinterpret_cast<uint8_t *>(slot_va + PAGE_SIZE);
tcb->kernel_stack_top = slot_va + SLOT_SIZE;

// Copy parent's stack content.
__builtin_memcpy(tcb->kernel_stack, parent->kernel_stack, STACK_SIZE);
```

## 6. Context Switch Changes

### 6.1 TSS RSP0

`GDT::set_tss_rsp0()` already sets RSP0 to `next->kernel_stack_top`
(`scheduler.cpp` line 1743).  No change needed — the new `kernel_stack_top`
is the top of the slot, which is the same as the current value.

### 6.2 RSP validation

The existing `rsp_in_stack_range()` function (`scheduler.cpp` line ~1446)
checks `rsp >= kernel_stack && rsp <= kernel_stack_top`.  The guard page
is BELOW `kernel_stack`, so an RSP that lands in the guard page (or below)
produces `rsp < kernel_stack` → returns false → corruption counter
increments.  This is correct — the scheduler already detects RSP drift
into the guard region as a corruption.

## 7. Alternative: Software Canary

For comparison, a simpler approach that avoids the separate VA range:

1. Place a canary value (`0xDEADBEEFDEADBEEF`) at the bottom 8 bytes of
   the kernel stack (at `kernel_stack`).
2. Check the canary on every context switch (in `switch_to_task()`):
   ```cpp
   if (*reinterpret_cast<uint64_t *>(current->kernel_stack) != CANARY_VALUE) {
       if (stack_overflow_hook) stack_overflow_hook(current);
       panic("kernel stack overflow (canary)");
   }
   ```

**Pros:**
- No VA range changes, no guard page, trivial to implement
- Detects overflows on next context switch (not immediately)

**Cons:**
- Does NOT catch the overflow at the faulting instruction (silent corruption
  continues until the task yields)
- Canary can be overwritten by a lucky sequence that writes the canary
  value itself (extremely unlikely but possible)
- False negatives: a write that skips over the canary (e.g., `memset` with
  a large size) doesn't corrupt the canary but still corrupts adjacent pages

**Recommendation:** Use the **Guard Page** approach (Sections 2–6) for
production safety.  The canary can be added as a secondary defence layer
at low cost.

## 8. Snapshot Interaction

The private VA window allocates page table pages (PDPT, PD, PT) from PMM via
`VMM::map_page()`.  The test isolation framework (`snapshot_restore`) restores
the PMM bitmap to its pre-test state, marking these page table pages as free.
After restore, PML4 entries for the kernel-stack window point to freed memory.

**Solution:** Only use the private window when no snapshot exists
(`!Scheduler::is_test_active()`).  In practice:
- **Boot / production:** `is_test_active() == false` → **private window** ✅  
  Tasks are never snapshot-restored; page tables persist for system lifetime.
- **Test execution:** `is_test_active() == true` → **HHDM**  
  Test tasks are cleaned up by `drain_zombie_list()` before `snapshot_restore`.
  Boot tasks (created before `snapshot_create`) use HHDM.
- **Dedicated guard-page tests (no snapshot):** Use `selftest` / `safe` classes
  or a dedicated test class that runs without snapshot isolation.

**snapshot_restore sequence:**
1. `drain_zombie_list()` — frees all test tasks (unmaps their window pages)
2. PMM restore — page-table pages for the window are now free
3. **Clear PML4[288]** — disconnects from freed page table pages (safe: no
   active tasks use the window at this point)
4. Scheduler state restore — boot tasks (HHDM) are restored
5. Next test creates tasks via HHDM

## 9. Implementation

### Phase 1 — Per-priority stack sizing (HHDM) ✅ Done
- `CONFIG_STACK_SIZE_TABLE` maps priority → stack size
- `stack_size_for_priority()` in `task.cpp`
- All stacks via HHDM, no private window
- 53/53 scheduler, 132/132 selftest, 42/42 ipc, 47/47 memory

### Phase 2 — Private VA window (conditionally enabled)
1. Add `kstack_slot_va_`, `kstack_slot_size_` to TCB
2. Indexed-pool slot allocator (`alloc_kslot` / `free_kslot`)
3. `VMM::map_page` in `create()` when `!is_test_active()`
4. `VMM::unmap_page` + poison + `free_kslot` in `cleanup()`
5. Guard-page detection in #PF handler (dormant when HHDM)
6. Clear PML4[288] in `snapshot_restore` after PMM restore

### Phase 3 — Tests (no-snapshot classes)
- `test_stack_guard_page_triggers_panic` — overflow via deep recursion
- `test_stack_guard_page_normal_operation` — normal depth, no fault

## 9. Risks and Caveats

1. **HHDM aliasing:** The physical pages are still accessible via
   `HHDM_OFFSET + phys`.  A wild pointer that writes to the HHDM alias of
   the guard page bypasses the guard completely.  Mitigation: no kernel
   code should access `stack_phys_` via HHDM — only via `kernel_stack`.
   The existing code already uses `stack_phys_` only for PMM alloc/free.

2. **Slot allocation failure:** If the bitmap is exhausted (all slots in
   use), `create()` fails gracefully — same as PMM OOM.

3. **Performance:** `VMM::map_page()` on create and `VMM::unmap_page()` on
   cleanup add ~1 µs per 16 stack pages (TLB shootdown via `invlpg`).
   This is acceptable for task creation/destruction (infrequent).

4. **VA window sizing:** 8 MiB window × 64 tasks = conservative upper bound
   of 128 KiB per task, well within the kernel virtual address space.
   Actual physical usage is `stack_size + 4 KiB (guard)`, sized per-task.
   The bump + free-list design handles fragmentation for the common
   create-all-at-boot / free-all-at-shutdown lifecycle.

5. **Double-fault:** If the guard-page #PF occurs while the kernel is
   handling a different interrupt (nested), the CPU attempts a double-fault
   (#DF).  The double-fault handler runs on the dedicated IST1 stack
   (4 KiB `df_stack`).  If the stack overflow was in the #PF handler itself
   (recursive), #DF may also overflow.  Mitigation: keep the guard-page
   detection path minimal and avoid deep calls.

6. **Idle-task deferred cleanup interaction:** Stack freeing already happens
   in idle context via the ZombieList path (`cleanup_step()` →
   `task->cleanup()` → frees stack pages).  This is correct and unchanged.
   The VMM unmap operations (16× `invlpg` per 64 KiB stack) should be
   **batched** within `cleanup()` to avoid per-page TLB flushes:

   ```cpp
   // Batch unmap then single TLB flush (invpcid or mov cr3)
   arch::IrqGuard guard;
   for (size_t i = 0; i < pages; ++i)
       page_table_clear(slot_va + PAGE_SIZE + i * PAGE_SIZE);
   arch::tlb_flush_global();   // single sync
   ```

   Stack **allocation** remains synchronous (the calling task needs the
   stack immediately) and is not deferred to idle.
