# Kernel Stack Window Page Table Pool

## Problem

The private kernel-stack window (0xFFFF900000000000, 16 MiB) needs page
table pages (PDPT, PD, PT) to map individual stack pages.  Currently
`VMM::map_page()` allocates these from PMM on demand.  When the test
framework calls `snapshot_restore()`, the PMM bitmap is restored to its
pre-test state — marking those page-table pages as free while the kernel
PML4 still points to them.  The next `VMM::map_page()` follows stale
pointers and crashes.

## Solution: Static Page Table Pool

Pre-allocate ALL page-table pages for the kernel-stack window at boot
time, using `PMM::alloc_page_table()` (from the existing page-table
pool).  These pages are never returned to PMM — they survive
snapshot_restore because the page-table pool is excluded from the
PMM bitmap restoration.

### Required Pages

The window (16 MiB at 0xFFFF900000000000) needs:

| Level | Pages | Size | Notes |
|-------|-------|------|-------|
| PML4  | 1 entry | — | Already exists in kernel PML4 at index 288 |
| PDPT  | 1 | 4 KiB | Covers 512 GiB, only 16 MiB used |
| PD    | 1 | 4 KiB | Covers 1 GiB |
| PT    | 8 | 32 KiB | Each covers 2 MiB, 8×2=16 MiB |
| **Total** | **10** | **40 KiB** | |

### Pool Source

Use `PMM::alloc_page_table()` which draws from
`CONFIG_PAGE_TABLE_POOL_SIZE` (default 4096 pages = 16 MiB).  This pool
is NOT restored by `snapshot_restore` — only the general PMM bitmap is
restored.  Page-table pages are excluded.

Verify in `pmm.cpp` that `PMM::restore_bitmap()` (called by
`snapshot_restore`) does NOT modify the page-table pool bitmap.

### Initialization

Add `init_kstack_window()` called from `VMM::init()` or
`Scheduler::init()`:

```cpp
void init_kstack_window() {
    // 1. Allocate PDPT
    uint64_t pdpt_phys = PMM::alloc_page_table();
    auto *pdpt = (uint64_t *)(HHDM_OFFSET + pdpt_phys);
    __builtin_memset(pdpt, 0, PAGE_SIZE);

    // 2. Allocate PD
    uint64_t pd_phys = PMM::alloc_page_table();
    auto *pd = (uint64_t *)(HHDM_OFFSET + pd_phys);
    __builtin_memset(pd, 0, PAGE_SIZE);

    // 3. Allocate 8 PT pages
    static uint64_t s_kstack_pt_pages[8];
    for (int i = 0; i < 8; ++i) {
        s_kstack_pt_pages[i] = PMM::alloc_page_table();
        auto *pt = (uint64_t *)(HHDM_OFFSET + s_kstack_pt_pages[i]);
        __builtin_memset(pt, 0, PAGE_SIZE);
        pd[0] = s_kstack_pt_pages[i] | PAGE_PRESENT | PAGE_WRITE;
    }

    // 4. Wire into kernel PML4
    auto *pml4 = (uint64_t *)(HHDM_OFFSET + (kernel_pml4_ & ~0xFFFULL));
    uint64_t base_48 = CONFIG_KSTACK_WINDOW_BASE & 0x0000FFFFFFFFFFFFULL;
    uint64_t pml4_idx = (base_48 >> 39) & 0x1FF;
    uint64_t pdpt_idx = (base_48 >> 30) & 0x1FF;
    pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITE;
    pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITE;
}
```

### Mapping Function

Replace the `VMM::map_page` calls in `create()` with a function that
writes directly to the pre-allocated PT pages:

```cpp
static void map_kstack_page(uint64_t virt, uint64_t phys) {
    // Compute PT index within the window.
    // PT[i] covers virt_base + i * 2MiB .. virt_base + (i+1) * 2MiB
    uint64_t offset = virt - CONFIG_KSTACK_WINDOW_BASE;
    size_t pt_idx = offset / (512 * PAGE_SIZE);  // which PT page
    size_t entry  = (offset / PAGE_SIZE) & 0x1FF;  // entry within PT
    auto *pt = (uint64_t *)(HHDM_OFFSET + s_kstack_pt_pages[pt_idx]);
    pt[entry] = phys | PAGE_PRESENT | PAGE_WRITE;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}
```

### Unmapping Function

```cpp
static void unmap_kstack_page(uint64_t virt) {
    uint64_t offset = virt - CONFIG_KSTACK_WINDOW_BASE;
    size_t pt_idx = offset / (512 * PAGE_SIZE);
    size_t entry  = (offset / PAGE_SIZE) & 0x1FF;
    auto *pt = (uint64_t *)(HHDM_OFFSET + s_kstack_pt_pages[pt_idx]);
    pt[entry] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}
```

### Snapshot Interaction

With the page-table pool excluded from PMM restore, `snapshot_restore`
no longer frees the kernel-stack-window page tables.  The PML4[288]
entry survives restore.  The PT entries set during test execution
(by `map_kstack_page`) are cleared during cleanup (by
`unmap_kstack_page` via `drain_zombie_list`).

**No PML4 clear needed** in `snapshot_restore` — remove the dormant
PML4 clear code added previously.

### Safety

- Boot tasks and test tasks both use the same page-table pool.
- Since the pool is never freed, both survive snapshot_restore.
- Only PT entries change dynamically (on task create/cleanup).
- PDPT, PD, and PT metadata pages are constant after boot.

### Test

Tests run under snapshot isolation as normal.  The private window is
used for ALL tasks (boot + test), which exercises the guard page
detection in the #PF handler.

Dedicated test (no snapshot):
- `test_stack_guard_page_triggers_panic` — create a task with minimal
  stack, cause deep recursion, verify guard page hits and `panic`
- `test_stack_guard_page_normal_operation` — normal stack depth,
  no fault

### Architecture Portability

The approach works identically on x86_64, aarch64, and riscv64:

| Component | x86_64 | aarch64 | riscv64 |
|-----------|--------|---------|---------|
| Page table pool | `PMM::alloc_page_table()` | same | same |
| Pre-alloc window pages | 10 (PDPT+PD+8×PT) | 10 (PUD+PMD+8×PT) | 9 (L1+L2+8×L3) |
| Base address | 0xFFFF900000000000 | 0xFFFF900000000000 | 0xFFFFFFC000000000 |
| TLB flush | `invlpg` | `tlbi vmalle1` | `sfence.vma` |
| #PF handler | vector 14 | ESR_EL1[31:26]=0b100101 | scause=12/13 |

### Implementation Order

1. Add `init_kstack_window()` called from `VMM::init()` or `Scheduler::init()`
2. Add `map_kstack_page()` / `unmap_kstack_page()` in `task.cpp`
3. Replace `VMM::map_page`/`unmap_page` calls in `create()`/`cleanup()`
4. Set `use_window = true` in `create()`
5. Remove PML4 clear from `snapshot_restore`
6. Enable `#PF` guard detection (already in kernel.cpp, dormant)
7. Tests
