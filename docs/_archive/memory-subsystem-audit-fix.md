# Memory Subsystem Audit — Fix Specification

**Audit Source:** `audits/memory_audit.md` (11 findings)  
**Status:** v0.3.6 Implementation Plan  
**Target:** Hard real-time compliance (ASIL-D / IEC 61508 SIL 4)

---

## 1. VULN-001 — MemPool Bitmap Out-of-Bounds (CRITICAL)

### Problem
Pool class 2 (64-byte blocks) has `block_count = 320` (bumped from 64 in v0.3.6). But `freed_bitmap[4]` and `pinned_bitmap[4]` provide only 256 bits. Accessing bits 256-319 writes one element past the array bound:

```cpp
freed_bitmap[idx / 64]  // idx=256 → freed_bitmap[4] → OOB!
```

This corrupts adjacent static storage (`pinned_bitmap[0]` in `Pool`, out-of-struct memory in `PoolMeta`).

### Fix
Replace hardcoded `4` with a `constexpr` computed from the maximum block count:

```cpp
// mempool.hpp
static constexpr size_t MAX_BLOCKS_PER_POOL = 320;
static constexpr size_t BITMAP_WORDS = (MAX_BLOCKS_PER_POOL + 63) / 64; // = 5
```

Change:
- `freed_bitmap[4]` → `freed_bitmap[BITMAP_WORDS]` in `Pool`
- `pinned_bitmap[4]` → `pinned_bitmap[BITMAP_WORDS]` in `Pool`
- `freed_bitmap[4]` → `freed_bitmap[BITMAP_WORDS]` in `PoolMeta` (mempool.hpp or .cpp)
- All `for (int i = 0; i < 4; ++i)` loops → `for (int i = 0; i < BITMAP_WORDS; ++i)` in `copy_freed_bitmap`, `write_freed_bitmap`, `clear_pinned_bitmap`
- Add `static_assert` in `MemPool::init()`: `counts[i] <= MAX_BLOCKS_PER_POOL`

### Files
`src/kernel/memory/mempool.hpp`, `src/kernel/memory/mempool.cpp`

### Risk
Low — mechanical substitution. The `PoolMeta` struct in `test_isolate.hpp` also has a `freed_bitmap[4]` that must be updated.

---

## 2. VULN-002 — Unsynchronized PMM/MemPool Mutation (CRITICAL)

### Problem
`bitmap_set/clear/test`, `owner_set_user/kernel`, `try_alloc_kernel/user`, `alloc_page/contiguous/user_page/free_page`, and all `MemPool::alloc/free/reserve/pin/unpin` mutate shared static state without any lock or IRQ disable. A preempting ISR that calls any of these produces torn read-modify-write → double-allocation or lost free entries.

### Fix
1. Introduce a POD `SpinLock` (TAS-based, `constinit`, no heap): `src/kernel/sync/spinlock.hpp`
2. Add `static SpinLock pmm_lock_` in `pmm.cpp`; wrap all mutating entry points (20+ functions) in `IrqSpinLockGuard(pmm_lock_)`
3. Add `static SpinLock mempool_lock_` in `mempool.cpp`; wrap `alloc/free/reserve/pin/unpin`
4. OOM handler callbacks must release lock before calling handler, re-acquire for retry

### Dependency
No external dependencies — can be built alongside VULN-001.

### Files
`src/kernel/sync/spinlock.hpp` (new), `src/kernel/memory/pmm.cpp`, `src/kernel/memory/mempool.cpp`

### Risk
High — adding locks to every allocation path risks deadlock and performance regression. Must verify:
- No lock is held across scheduling operations
- `IrqSpinLockGuard` correctly saves/restores interrupt state
- No recursive locking in `alloc_page` → OOM handler → `alloc_page`

---

## 3. VULN-003 — Linear-Scan Page Allocation (HIGH)

### Problem
`try_alloc_kernel/user` perform O(`total_pages_`) linear bitmap scans. Worst-case WCET depends on memory size and fragmentation.

### Fix
Replace the bitmap scan with an O(1) intrusive free-list:
1. Reserve `total_pages_ * sizeof(uint64_t)` bytes adjacent to the existing bitmap region (or reuse a region within the reserved area) for `free_list_next[]`
2. During `PMM::init()`, build the free list by linking all free pages
3. `try_alloc_kernel(1)` / `try_alloc_user(1)`: pop from free-list head, update bitmap for introspection only
4. `free_page()`: push freed index onto free-list head
5. For contiguous allocation (`alloc_contiguous`), keep the bitmap scan fallback but add a documented WCET bound comment
6. `alloc_page_table()`'s pool scan: replace with O(1) free-list over `CONFIG_PAGE_TABLE_POOL_SIZE` range

### Dependency
Requires VULN-002 (lock protects free-list mutations). The free-list storage must be allocated during `PMM::init()` from the reserved region.

### Files
`src/kernel/memory/pmm.cpp`, `src/kernel/memory/pmm.hpp`

---

## 4. VULN-004 — Missing Ownership Validation in map_page (HIGH)

### Problem
`VMM::map_page()` and `VMM::map_page_in_pml4()` do not check that `phys_addr` is a USER-owned page when `user=true`. A caller can map a KERNEL-owned page into user space, breaking Ring-3 isolation.

### Fix
Add unconditional check before the leaf PTE write:

```cpp
if (user) {
    ENSURE(PMM::is_user_page(phys_addr) &&
           "map_page: attempt to map KERNEL-owned physical page as user-accessible");
}
```

Apply in both `map_page()` (all 3 arch branches) and `map_page_in_pml4()`.

### Dependency
Independent.

### Files
`src/kernel/memory/vmm.cpp`

### Risk
Low — pure assertion, no behavioral change for correct callers. May expose latent bugs in tests that map kernel pages as user-accessible.

---

## 5. VULN-005 — Non-Atomic Memory Budget Counter (MEDIUM)

### Problem
`#if CONFIG_MEMORY_BUDGET` block in `alloc_page()`/`alloc_contiguous()` stashes `cur = Scheduler::current_task()` across a call to `try_alloc_kernel()`. If the task exits between the two, `cur` is stale → corrupts unrelated TCB.

### Fix
After VULN-002's `IrqSpinLockGuard` protects the entire budget check → allocation → increment sequence, keep a single `cur` pointer inside the critical section (no preemption possible). Remove the second `Scheduler::current_task()` fetch.

### Dependency
Depends on VULN-002 (SpinLock + IrqGuard).

### Files
`src/kernel/memory/pmm.cpp`

---

## 6. VULN-006 — Unbounded WCET in Address-Space Teardown/Clone (MEDIUM)

### Problem
`free_user_pages()` and `deep_copy_user_pages()` traverse up to 512³–512⁴ page-table entries without yielding. Process creation/exit latency grows with mapped memory.

### Fix
1. Add WCET-bound comment: `// Worst-case iterations: PML4_USER_COUNT * 512^3 leaf visits`
2. Insert cooperative yield point every 64 leaf/table entries: `if (++yield_counter % 64 == 0) Scheduler::maybe_preempt();`

### Dependency
Independent.

### Files
`src/kernel/memory/vmm.cpp`

---

## 7. VULN-007 — Unbounded Scans in reserve/pool_used_pages (LOW)

### Problem
`MemPool::reserve()` and `PMM::pool_used_pages()` linearly scan without a boot-phase-only gate.

### Fix
1. `reserve()`: add `ENSURE(!ready_ && "...")` at entry
2. `pool_used_pages()` / `pool_total_pages()`: add `@note Diagnostic/introspection only — O(pool_size_pages), must not be called from WCET-budgeted hot path` doc comment

### Dependency
Independent.

### Files
`src/kernel/memory/mempool.cpp`, `src/kernel/memory/pmm.hpp`

---

## 8. VULN-008 — Silent No-Op Free on Pinned Blocks (LOW)

### Problem
`MemPool::free()` silently ignores free of pinned blocks. `pin_block()` doesn't verify the block is allocated before pinning.

### Fix
1. `pin_block()`: add `ENSURE(!pool.is_block_freed(block_idx) && "...")` before marking pinned
2. `free_err()`: return `MEMPOOL_ERR_PINNED` instead of `MEMPOOL_ERR_OK`
3. `free()`: add `Logger::warn(...)` on the pinned branch

### Files
`src/kernel/memory/mempool.cpp`, `src/kernel/memory/mempool_errors.hpp`

---

## 9. VULN-009 — free_page_err Missing owner_set_kernel (LOW)

### Problem
`free_page_err()` clears the bitmap bit but does not reset the owner bit to KERNEL, unlike `free_page()`.

### Fix
Add `owner_set_kernel(index);` after `bitmap_clear(index);` in `free_page_err()`.

### Files
`src/kernel/memory/pmm.cpp`

### Risk
Trivial one-liner.

---

## 10. VULN-010 — Idle Loop Control Flow (LOW)

### Problem
`for (uint64_t _i = 0; _i < UINT64_MAX; ++_i)` misleads WCET tooling into treating the idle loop as bounded.

### Fix
Replace with `for (;;)` and a `// [[noreturn]]` comment.

### Files
`src/kernel/memory/integrity.cpp`

---

## 11. VULN-011 — Unsynchronized CRC State (LOW)

### Problem
File-scope `crc_accumulator`, `crc_offset`, etc. are mutated without documentation or enforcement of single-writer invariant.

### Fix
Add reentrancy guard flag + assertion in `reset_crc_state()` and `crc_process_chunk()`. Add doc comment above statics.

### Files
`src/kernel/memory/integrity.cpp`

---

## Implementation Order for v0.3.6

The dependencies between findings determine the order:

```
Phase 1 — Independent fixes (can be done in any order):
  VULN-001  MemPool bitmap OOB          [CRITICAL]
  VULN-004  Ownership check in map_page  [HIGH]
  VULN-006  Yield in free_user_pages     [MEDIUM]
  VULN-007  Boot-phase gate in reserve   [LOW]
  VULN-008  Pinned-block diagnostics     [LOW]
  VULN-009  free_page_err owner_set      [LOW]
  VULN-010  Idle loop style              [LOW]
  VULN-011  CRC reentrancy guard         [LOW]

Phase 2 — Lock infrastructure (prerequisite for Phase 3):
  VULN-002  SpinLock + PMM/MemPool locks [CRITICAL]

Phase 3 — Depends on VULN-002:
  VULN-003  O(1) free-list allocator     [HIGH]
  VULN-005  Atomic memory budget counter [MEDIUM]
```

Within each phase, findings are listed by severity (CRITICAL → HIGH → MEDIUM → LOW).
