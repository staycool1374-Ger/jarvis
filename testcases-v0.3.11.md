# Test Cases — v0.3.11 (BufferPool user-stack PT-page +1 leak)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

## Objective

Eliminate the **+1 PMM page residual** that every `create_user` + 
`BufferPool::alloc` test reports.  Control experiment proves it is a REAL lost
page (kernel task + `clone_kernel_pml4()` = 0 leak; `create_user` = +1), NOT a
ResourceTracker artifact.  Only the BufferPool/`free_user_pages` page-table
lifecycle is in scope.

## Evidence recap (from v0.3.10)

- `create_user` PMM 1008 → 1036 (+28): clone PML4 (1) + user stack data 32 KiB
  (8) + kernel stack 64 KiB (16) + stack-region PDPT/PD/PT (3).
- `BufferPool::alloc` maps buffer into the SAME PML4
  (`VMM::map_page_in_pml4`, buffer_pool.cpp:305), adding PT pages for the
  buffer VA.
- `free_user_pages` (vmm.cpp:614) frees 5 pages for a normal
  create_user+buffer; after `buffer_pool_exhaustion` (1024 buffers / 4 MB /
  8 PT pages) the next tests free only 4 → 1 PT page (pd=2,3 under pdpt=1)
  escapes.
- `buffer_pool_exhaustion` +896 → +1 after the v0.3.10 `free_page()`
  overflow-to-PMM fix.

## Test Cases

### B1 — Owner-bit drift on recycled PT pages
- **Testidea:** After pool-overflow free (owner → KERNEL) and re-map, the PT
  page owner bit stays KERNEL so `free_user_pages` skips it.
- **Input:** run `buffer_pool_exhaustion` then `buffer_pool_double_free`;
  inspect `PMM::is_user_page(pt_phys)` for pd=2,3/pdpt=1 at cleanup.
- **Expect:** all PT pages under a user mapping are USER-owned; `free_user_pages`
  frees every one; tracker delta 0.
- **Depends:** `VMM::free_user_pages`, `PMM::owner_*`, `BufferPool::free_page`.

### B2 — Shared-PDPT aliasing (stack + buffers both under pdpt=1)
- **Testidea:** User stack (pd=384) and buffers (pd=0..3) share PDPT entry 1;
  verify the walk covers all PD entries after `unmap_all` clears only the leaf.
- **Input:** after exhaustion, dump PDPT[1] and each PD[idx] present bit before
  the next cleanup.
- **Expect:** PD entries for every buffer PT page remain present and are freed.
- **Depends:** `VMM::free_user_pages`, `BufferPool::unmap_all`/`clear_pte_in_pml4`.

### B3 — 4 MB / 8-PT-page walk coverage
- **Testidea:** exhaustion maps 0x40000000..0x40000000+4 MB spanning PD 0..3;
  verify `map_page_in_pml4` creates 4 PT pages and `free_user_pages` frees all.
- **Input:** count PT pages under pdpt=1/pd=0..3 at alloc and at cleanup.
- **Expect:** alloc count == free count for every buffer test.
- **Depends:** `VMM::map_page_in_pml4`, `VMM::free_user_pages`.

### B4 — Full-class leak-clean gate
- **Testidea:** the entire `buffer_pool` class reports ZERO PMM leaks.
- **Input:** `make execute-test x86_64 debug buffer_pool`.
- **Expect:** 24/24 PASS with no `[RESOURCE] ... PMM pages` WARN lines.
- **Depends:** all B1-B3.

## GDB Plan

`make debug-test x86_64 debug buffer_pool tools/gdb/test-batch.gdb` with a
breakpoint at `VMM::free_user_pages`.  After exhaustion's cleanup, walk
PML4[0]→PDPT[1]→PD[0..3]→PT pages; print each `pt_phys` and its owner bit
(`PMM::is_user_page`).  This deterministically shows whether the missing PT
page is (a) not walked, (b) KERNEL-owned, or (c) recycled/dangling.

## Acceptance

- `buffer_pool` 24/24, zero PMM WARN.
- `memory` 47/47, `selftest` 132/132, `vfs` 146/146.
- test-history rows appended.
