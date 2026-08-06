# Fork Page-Table Deep Copy

## Problem

`fork()` currently copies only the **PML4 entries** from parent to child.
The child's user-space page-table hierarchy (PDPT, PD, PT pages) consists
of the **same physical pages** as the parent — they are shared.  A single
`VMM::map_page_in_pml4()` call in the child (e.g. from `brk` or `mmap`)
modifies a shared page-table page and corrupts the parent's address space.

The `page_table_shared_` flag prevents `cleanup()` from double-freeing page
tables, but it does not prevent corruption.  A private-PDPT hack protects only
the stack region.

## Design

Replace the shallow PML4-entry copy with a **recursive deep copy** of the
entire user-space page table hierarchy.  After fork, parent and child have
completely independent page table trees, each pointing to its own physical
copies of data pages.

### Page Table Structure (x86_64)

```
PML4 (512 entries × 8 bytes = 4 KiB)
  │  user entries: indices 0–255  (512 GiB)
  │  kernel entries: indices 256–511 (shared with kernel PML4)
  │
  └── PDPT (512 entries × 8 bytes = 4 KiB)     ← allocate new for child
        │  each entry covers 1 GiB
        │
        └── PD (512 entries × 8 bytes = 4 KiB)  ← allocate new for child
              │  each entry covers 2 MiB
              │
              └── PT (512 entries × 8 bytes = 4 KiB)  ← allocate new for child
                    │  each entry covers 4 KiB
                    │
                    └── 4 KiB data page           ← allocate new + memcpy
```

### Algorithm (Eager Copy)

```
function deep_copy_pt(parent_pml4_phys, child_pml4_phys):
    for each user PML4 entry i (0..arch::PML4_USER_COUNT-1):
        if parent entry is not present:
            child entry = 0 (not present) — skip
            continue

        if parent entry is a 1 GiB huge page:
            split_huge_1gb(parent_entry, child)
            continue

        // Find or create child PDPT
        child_pdpt_phys = alloc_user_pt_page()    // PMM::alloc_user_page()
        child_pml4[i] = child_pdpt_phys | flags   // set in child PML4

        for each PDPT entry j (0..511):
            if parent PDPT entry is not present: skip
            if parent PDPT entry is 1 GiB huge page:
                split_huge_1gb(parent_entry, child_pdpt)
                continue

            // Find or create child PD
            child_pd_phys = alloc_user_pt_page()
            child_pdpt[j] = child_pd_phys | flags

            for each PD entry k (0..511):
                if parent PD entry is not present: skip
                if parent PD entry is 2 MiB huge page:
                    split_huge_2mb(parent_entry, child_pd)
                    continue

                // Find or create child PT
                child_pt_phys = alloc_user_pt_page()
                child_pd[k] = child_pt_phys | flags

                for each PT entry l (0..511):
                    if parent PT entry is not present: skip

                    // Allocate new data page + copy content
                    new_data_phys = PMM::alloc_user_page()
                    copy via HHDM:
                        memcpy(HHDM_OFFSET + new_data_phys,
                               HHDM_OFFSET + parent_data_phys,
                               4096)
                    child_pt[l] = new_data_phys | flags
```

### Huge Page Splitting

When a parent's PD entry is a 2 MiB huge page:
1. Allocate a new PT page (512 entries × 4 KiB = 2 MiB covered)
2. For each of the 512 PT entries, set the physical address to
   `huge_base + i * 4096` with the same flags
3. Set the child's PD entry to point to the new PT page
4. For each of the 512 4 KiB sub-pages, allocate a new physical page
   and copy content

Same logic for 1 GiB huge pages (split into 512 PD entries, each
of which may be further split).

### User-Page Ownership

All newly allocated page-table pages MUST use `PMM::alloc_user_page()`
(or `PMM::alloc_page_table()` with user ownership tracking).  Pages
allocated via `PMM::alloc_user_contiguous()` or `PMM::alloc_user_page()`
are marked as **user-owned** in the owner bitmap.

This ensures `VMM::free_user_pages()` (called from `cleanup()`) can
correctly walk and free the entire private hierarchy.

### `page_table_shared_` After Deep Copy

After deep copy, parent and child have independent page table trees.
`page_table_shared_` is set to `false` for the child.  `cleanup()` will
call `VMM::free_user_pages(child->page_table_)` which frees all user-
owned page-table pages and data pages.

The `page_table_shared_` field is **retained** for:
- The reaper's child-sharing check (`scheduler.cpp` line 1361) — a
  non-sharing child can be reaped immediately.
- `exec_into_current()` — saves/restores the flag across exec.

### Removal of Stack-Private PDPT Hack

The current code allocates a **private PDPT** for the user stack region
(lines 942-964 of `task.cpp`).  With full deep copy, ALL regions get
private page tables.  The stack-private-PDPT code can be removed.

### COW (Copy-on-Write) — Future Work

Eager copy duplicates all physical data pages at fork time.  This is
simple but doubles memory usage and makes fork slow.

COW approach (for a later version):
1. Deep-copy the page table hierarchy as above (new PDPT/PD/PT pages).
2. **PT entries** point to the SAME physical data pages as the parent,
   but are marked **read-only** (clear `PAGE_WRITE`).
3. On write fault, the #PF handler:
   a. Detects a COW page (read-only mapped, but writable in VMA)
   b. Allocates a new physical page
   c. Copies content from the shared page
   d. Maps the new page as writable in the faulting task's PT
4. The page is no longer shared — each task gets its own copy on first
   write.

Changes needed for COW:
- PT entries marked `read-only` after fork
- #PF handler needs a COW detection path (check VMA permissions)
- Reference counting or page-fault frequency tracking for shared pages

## Implementation Plan

### Phase 1 — Eager Copy
1. Implement `VMM::deep_copy_user_pages(src_pml4, dst_pml4)`
2. Call it from `TaskControlBlock::clone()` instead of the shallow copy
3. Remove `page_table_shared_ = true` from clone (child owns its tables)
4. Remove stack-private-PDPT hack
5. Update tests to verify independence
6. Remove stale `page_table_shared_` references where possible

### Phase 2 — Cleanup
7. Remove `page_table_shared_` field if no longer referenced
8. Clean up reaper's sharing checks
9. Update ROADMAP

### Phase 3 — COW (Future)
10. COW page fault handler
11. Reference counting for shared data pages
12. Performance benchmarking
