# Cumulative Corruption Investigation Log

**Start:** 2026-07-30
**Symptom:** `all` test class GPF at test ~847 in `snapshot_restore` reading corrupted `nu` value (`0x5F580000FC00C180` pattern)
**Root cause:** Not yet found — this document tracks attempts.

---

## Attempt 1: nk offset mismatch

**Observation:** `misc[0]` (task count) is overwritten by the refresh block with post-reload count, but `off_pt_pool()` and `off_hhdm_pd()` need the original count.

**Fix:** Read `nk` from kstack header (stable, never overwritten) instead of `misc[0]`. Also added `PtPoolSnapshot` recapture in refresh block.

**Result:** Pushed corruption boundary from test ~417 (0xDD stack poison) to test ~847 (user-task page fault / snapshot_restore GPF with `0x5F58...` garbage). The root cause still exists — it just manifests later.

---

## Attempt 2: Kernel stack page re-allocation

**Hypothesis:** PMM restore frees kernel stack pages, tests reallocate them, next restore's `memcpy(t->kernel_stack, ...)` overwrites test data.

**Evidence disproved:** Saved PMM bitmap has kernel stack pages as allocated (at snapshot time). After PMM restore, they remain allocated. They're never freed.

**Result:** False lead. Reverted.

---

## Attempt 3: `nu` field corruption diagnostic

**Observation:** The GPF at test ~847 reads garbage `nu` (`0x5F580000FC00C180`) from `g_snapshot + off_user_page_count()`. The `nu` field is **never written** after `snapshot_create`. Something writes to the wrong physical address.

**Diagnostic added:** Log `nu` value before each restore, dump surrounding memory if `nu > 100000`.

**Result:** (pending — diagnostic added but not yet run)

---

## Attempt 4: (future) GDB hardware watchpoint

**Plan:** Set a hardware watchpoint on `g_snapshot + off_user_page_count()` and run `all` under GDB. When the write occurs, GDB captures the exact instruction and backtrace.

**Why this works:** The `nu` field should NEVER be written to after `snapshot_create`. Any write to it is a bug. A hardware watchpoint catches it on the first occurrence, before cumulative damage propagates.

**Implementation:** Use `make debug-test x86_64 debug all tools/gdb/track-nu.gdb` with a GDB script:
```
set pagination off
break snapshot_restore
commands
  print *(uint64_t*)($rdi + off_user_page_count())
  watch *(uint64_t*)($rdi + off_user_page_count())
  continue
end
continue
```

---

## Patterns observed

| Test position | Fault type | Garbage pattern | Likely cause |
|---|---|---|---|
| ~417 (before nk fix) | GPF `rip=0xDDDDDDDDDDDDDDDD` | `0xDD` (MemPool poison) | Pool bitmap from wrong offset corrupted MemPool block_size → memset overflow |
| ~847 (after nk fix) | GPF in `snapshot_restore` | `0x5F580000FC00...` | `nu` field in snapshot buffer overwritten by stray write |
| ~847 (earlier, different code state) | GPF in `restore_pool_snapshot` | `0x5F578000FC8F8051` as `%rdi` | Same class: snapshot buffer corrupted |

All three exhibit the same root pattern: **snapshot buffer data corrupted by a write to the wrong physical address**. The `0x5F5...` pattern suggests a user-space pointer or MemPool data structure was written over the snapshot region.

**Leading hypothesis:** The PD restore writes to `pd + 1` = `HHDM_OFFSET + (pdpt[0] & ~0xFFFULL) + 8`. If `pdpt[0]` was corrupted to point to the snapshot buffer's physical address instead of `PD_HIGHER` (0x5000), the 4088-byte `memcpy` would overwrite the snapshot buffer. This would corrupt `nu`, `saved_pd`, `PtPoolSnapshot`, and everything after.

**Why `pdpt[0]` could be corrupted:** The PDPT page (physical 0x4000) is in the boot page table region. If a test allocates a physical page at 0x4000 (via PMM), it could overwrite the PDPT. But 0x4000 is within the first 2MB (PD[0]) and should be reserved. Unless `alloc_contiguous` for the snapshot buffer returns page 0x4000.

**Check:** Does `alloc_contiguous` ever return page 0x4000? The PMM bitmap says page 0x4000/4096 = page index 4. At boot, pages 0 through kernel_reserved_end are marked allocated. Page 4 (0x4000) is before the kernel image (0x100000), so it should be in the "below kernel" range which is allocated. But userspace pages below kernel_start should NOT be free.

---

## Attempt 4: 0xDD stack poison from task cleanup (ROOT CAUSE FOUND)

**Date:** 2026-07-30

**Root cause chain:**
1. Test creates task T → kernel stack allocated from PMM at physical page X
2. T terminates during daemon reload → `cleanup()` does `memset(HHDM_OFFSET+X, 0xDD, pages*4096)` → fills X with `0xDD`  
3. `PMM::free_page(X)` → X is now **free** but still contains `0xDD` (PMM doesn't zero freed pages)
4. `snapshot_restore` PMM bitmap restore → X already free in saved bitmap → X remains in free list with `0xDD` content
5. Next test allocates X from free list for a data structure (TCB, buffer, etc.)
6. Data structure's first 8 bytes = `0xDDDDDDDDDDDDDDDD` (the poison from step 2)
7. If X holds a struct with a function pointer → pointer = `0xDDDDDDDDDDDDDDDD` → GPF at that address
8. If X holds a TCB → magic = `0xDDDDDDDDDDDDDDDD` → `safe_tcb` fails → task removed from list

**Why it manifests at test ~417 specifically:**
Before ~417 tests, the free list is healthy enough that reallocations don't conflict. After ~417 cycles of allocation/free/recycle, the free list structure becomes favorable for a reallocation to land on a page that was poisoned by a recently-cleaned-up task.

**Why the nk fix didn't help:**
The 0xDD corruption is NOT from the PtPoolSnapshot offset. It's from the task cleanup's `memset(HHDM_OFFSET + stack_phys_, 0xDD, ...)` running BEFORE the PMM restore, poisoning the page, then the page being freed and reallocated with the poison still present.

**Fix:**
Remove the `0xDD` memset from `TaskControlBlock::cleanup()`. The poison persists in freed pages and leaks into reallocations. Use-after-free detection relies on TCB magic checks (`safe_tcb`) instead.

**Verification:**
After removing the memset, the `all` test should progress past test 417 without the 0xDD GPF. The remaining ~35 tests (848-882) are blocked by a separate user-task page fault issue at the static_pools → mlock boundary (847 crash).

---

## Attempt 5: (future) User-task page fault at test 847

**Symptom:** Task 0x1D (PID 29) tries to execute kernel address `0xFFFF80000021324D` from user mode (CR3=0x6F46000, err=0x15). Kernel sends SIGSEGV, task has no handler → kernel panics.

**Hypothesis:** A recreated daemon task (vfsd or iocd) has its user PML4 corrupted. The user PML4 shares kernel page table entries with the kernel PML4 (via `clone_kernel_pml4`). If the shared PD page was corrupted by the PD restore writing to the wrong address, the user task can't access kernel code.

**Status:** Not yet investigated. Requires GDB watchpoint or further static analysis.
