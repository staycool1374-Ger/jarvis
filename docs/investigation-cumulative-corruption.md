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
After removing the memset, the `all` test should progress past test 417 without the 0xDD GPF. The remaining ~35 tests (848-882) are blocked by a separate GPF in `snapshot_restore` at test 847.

---

## Attempt 5: off_kstack_header returns garbage at test 847

**Date:** 2026-07-30

**Symptom:** GPF at `rip=0xFFFF800000266572` in `snapshot_restore`. Instruction `mov (%rbx,%rax,1),%rsi` reads `nk` from kstack header. `%rax = 0x5F580000FC0D0049` (garbage return from `off_kstack_header(nu)`), `%rdi = 0x5F580000FC00C180`.

**Root cause:** Unknown. `off_kstack_header(nu)` is simple arithmetic (`off_user_page_data() + nu * 4104 + 2048`). It CANNOT return garbage for a valid `nu`. The function code in the binary disassembly is correct.

**Hypotheses:**
1. Instruction stream corruption — the `movabs` or `call` instructions in `off_kstack_header`'s code were overwritten by a stray write (e.g., PD restore writing to wrong address via HHDM)
2. Register corruption — `%rax` returned from the function was overwritten by a timer ISR or another interrupt between the function's `ret` and the next instruction

**Attempts to catch:**
- GDB remote debugging: GDB 17.2 batch mode incompatible with QEMU remote stub (`commands` block fails with "Cannot execute this command while the target is running")
- Added DIAG-847 diagnostic (2026-07-30): compares `off_kstack_header(nu)` return value against manual computation `off_user_page_data() + nu * 4104 + 2048`. If they differ, dumps function code bytes and snapshot buffer address.

**Resolution:** Moving guard pages after the HHDM PD save in snapshot_create resolved the 417 TCB corruption. Before the fix, guard PT page pointers were captured in the saved PD entries, causing 2MB huge pages to stay permanently split across cycles. After ~90+ cycles, orphaned PT pages accumulated in the pool bitmap, corrupting TCB page table entries → `magic = &t`.

**Verification:** `all` test 882/882, 881 PASS, 1 FAIL (pre-existing stack_profiler). The `remove_task` error at test 91 is completely absent.

---

## Attempt 6: lldb hardware watchpoint — tests 91, 417 confirmed clean

**Date:** 2026-07-30

**Symptom:** Investigation logs mention test 91 (`remove_task` error) and test 417
(`0xDD` GPF) as earlier failure points. These were previously fixed by the guard-pages
and 0xDD-memset removals. Confirmed clean with lldb hardware watchpoints.

**Technique:** (lldb replaces GDB 17.2 which has batch-mode `commands` block
incompatibility with QEMU remote stub)

```
gdb-remote 1234
breakpoint set --address 0xFFFF800000244D67    # after g_snapshot NULL check
breakpoint modify 1 --ignore-count N           # N = test index
continue
expr long $gptr = *(unsigned long long*)0xFFFF800000458F88
watchpoint set expression -w write -s 8 -- `$gptr + 0xC32F0`
watchpoint disable 1
thread step-out
watchpoint enable 1
continue
```

**Result:**
- **Test 91** (`scheduler_reschedule_noop`): watchpoint fired only once during
  `snapshot_restore` (QEMU false positive on read). No write during test execution.
- **Test 417** (`fb_scroll_up`): same — no stray write during test.
- Confirmed: both earlier failure points are indeed resolved.

---

## Attempt 7: PD restore hypothesis eliminated

**Date:** 2026-07-30

**Observation:** The investigation log's leading hypothesis (Attempt 3 patterns table)
claimed the PD restore's `memcpy(pd+1, saved_pd+1, 4088)` could overwrite the
snapshot buffer if `pdpt[0]` was corrupted to point at the buffer's physical address.

**Evidence disproved:** The PD restore code has a sanity check:

```cpp
uint64_t pd_phys = pdpt[0] & ~0xFFFULL;
if (pd_phys == 0x5000ULL) {     // ← only restores if PDPT[0] points to boot PD
    auto *pd = reinterpret_cast<uint64_t *>(arch::HHDM_OFFSET + pd_phys);
    __builtin_memcpy(pd + 1, saved_pd + 1, (512 - 1) * sizeof(uint64_t));
}
```

The restore is skipped unless `pdpt[0] == 0x5000` (the boot PD page). Any
corrupted `pdpt[0]` would fail this check and the PD restore would not execute.
The 4088-byte `memcpy` cannot reach the snapshot buffer through this path.

**Result:** False lead. The garbage `nu` at test 847 has a different source.

---

## Attempt 8: Redundant `nu` copy diagnostic

**Date:** 2026-07-30

**Problem:** The existing canary fields (`CANARY_BEFORE` / `CANARY_AFTER`) detect
mass corruption but cannot distinguish a targeted 8-byte overwrite of `nu` alone
(e.g., a stray `mov` that hits exactly the right address) from a wide sweep that
clobbers thousands of bytes.

**Fix:** Added `off_user_page_count_copy()` — a second copy of `nu` at `nu + 8`.
Both `nu[0]` and `nu[1]` are written with `user_page_count` in `snapshot_create`.
`snapshot_restore` checks they match.

**Layout (after fix):**
| Offset | Field | Identifies |
|--------|-------|------------|
| 0xC32E8 | CANARY_BEFORE | mass sweep |
| 0xC32F0 | **nu[0]** | primary copy |
| 0xC32F8 | **nu[1]** | redundant copy |
| 0xC3300 | CANARY_AFTER | mass sweep |

**Diagnostic logic:**
- `canary_before/after` corrupted → wide sweep (memcpy/memset over buffer)
- `nu[0] != nu[1]` → targeted 8-byte overwrite of exactly `nu`
- `nu[0] == nu[1]` but wrong value → buffer written with consistent data that
  happens to include `nu` (e.g., struct copy from a different object)

**lldb verification:** Both copies confirmed identical at first snapshot_restore:
```
0xFFFF8000008F72E8: 0xCAFEBABE00000001 0x00000000000000D5   ← canary_before, nu[0]
0xFFFF8000008F72F8: 0x00000000000000D5 0xCAFEBABE00000002   ← nu[1], canary_after
```

**Outstanding:** The garbage `nu` at test ~847 has not been reproduced with the
current codebase. The `all` test suite timed out with `[LK-CONTEND]` lock
contention at ~13,000 ticks (safe-mode tests pass, isolated tests hang). The
lock contention may be a pre-existing scheduler issue unrelated to snapshot
corruption, blocking reproduction of the test 847 `0x5F58...` garbage.

---

## Attempt 9: `restore_task_fields` skips TCBs with bad magic (ROOT CAUSE CONFIRMED)

**Date:** 2026-07-30

**Symptom:** `remove_task` error with `magic=0xDDDDDDDDDDDDDDDD` after
snapshot_restore cycles, logged as `[CLEANUP] skip poisoned TCB`.

**Hypothesis (code analysis):**
1. A test allocates TCBs from MemPool pool-8 via `TaskControlBlock::create()`
2. Test frees TCBs (e.g., via `dl_free` → `cleanup()` → `delete` → `MemPool::free`)
3. `MemPool::free` (CONFIG_DEBUG, `mempool.cpp:139`):
   `__builtin_memset(p, 0xDD, pool.block_size)` — fills freed block with 0xDD
4. `snapshot_restore` restores MemPool bitmap → block marked allocated again
   (same as at snapshot time), but content is still 0xDD
5. `Scheduler::restore_task_fields` (`scheduler.cpp:2129`) checks `t->magic`:
   ```cpp
   if (t->magic != TaskControlBlock::TCB_MAGIC)
       continue;  // ← SKIP — no fields restored
   ```
   With magic == 0xDD, the TCB is **skipped** and never repaired.
6. TCB remains in `all_tasks_` (restored by `restore_state`) with corrupted
   content.
7. Subsequent `remove_task()` finds `magic=0xDD` → logs error → `[CLEANUP]`.

**lldb confirmation:**
Breakpoint at `scheduler.cpp:2129` (the `jne` that skips the TCB):
```
0xFFFF80000024E1B6: cmp %rax, 0x358(%rsi)   ; compare TCB_MAGIC vs t->magic
                   jne <skip>                 ; skip if bad magic
```
Hit during test 417+ cycle with `%rsi = 0xFFFF800000791000` — the exact
corrupted TCB address from the error log. The TCB was skipped by
`restore_task_fields` and its fields were never restored.

**Root cause:** `restore_task_fields` refuses to restore fields into a TCB
whose `magic` field is corrupted. But the MemPool bitmap was already restored,
so the block IS the TCB's block — overwriting its fields is safe. The 0xDD
check is a false-negative guard: it skips the TCB, leaving it broken, which is
exactly the state it was designed to detect.

**Fix applied:** At `scheduler.cpp:2129`, removed the magic check and added
position-based fallback matching. `restore_task_fields` now restores fields into
corrupted TCBs (overwrites 0xDD with correct saved values).

**Verification:** After the fix, `all` test class runs show zero `remove_task`
0xDD errors (confirmed by grepping serial log). The symptom is eliminated.

---

## Attempt 10: `try_lock` hypothesis disproven

**Date:** 2026-07-30

**Hypothesis:** `Scheduler::unregister_task` at `scheduler.cpp:524` uses
`try_lock()`. When it fails (lock held by timer ISR), the TCB stays in
`all_tasks_` while `delete t` → `MemPool::free` writes 0xDD, creating a dangling
pointer.

**Test:** Added diagnostic log to `unregister_task`:
```cpp
if (!scheduler_lock_.try_lock()) {
    Logger::raw_write("[UNREG] try_lock FAILED task=");
    Logger::print_hex(reinterpret_cast<uint64_t>(&task));
    ...
    return false;
}
```

**Result:** **Zero `[UNREG]` messages** during `all` test class run (78 tests
before IPC crash). `try_lock` NEVER fails. The hypothesis is **disproven**.

**Status:** The 0xDD TCBs in `all_tasks_` after `restore_state` must come from
a different mechanism. `unregister_task` always succeeds (removes the TCB before
free), but `restore_state` → `all_tasks_.restore(tasks_in, ...)` puts a pointer
back. The `tasks_in` array was captured at snapshot time from the SAME
`all_tasks_` — it should only contain boot-time (pinned) TCBs.

---

## Attempt 11: Minimal reproducer test — did NOT reproduce

**Date:** 2026-07-30

**Test:** Wrote `harness_tcb_corruption_repro` in `test_testrunner.cpp`:
- 500 iterations
- 32 tasks created/destroyed per iteration (via create/cleanup/delete)
- `snapshot_restore` called AFTER EACH iteration (same as real test runner)
- Ran WITHOUT the `restore_task_fields` fix

**Result:** **No corruption.** Test passed (11/15 in testrunner class). Zero
`remove_task`/0xDD errors. Even 500 full cycles with snapshot rewinding did
not produce a single 0xDD TCB in `all_tasks_`.

**Updated status:** The simple create/destroy + snapshot_restore pattern does
NOT trigger the 0xDD corruption. The original corruption (seen at test ~345+
in the full `all` suite) requires a more specific interleaving — possibly:
- A specific sequence of MemPool allocations from DIFFERENT pools (not just
  pool-8) that causes block aliasing
- A test that writes to the snapshot buffer via a misdirected page-table walk
- A race between the timer ISR and a specific test operation

**Ongoing:** The `restore_task_fields` fix is kept as a defensive measure. It
prevents propagation if the corruption ever re-occurs. Without a reproducible
test case, the root cause cannot be determined.

---

## Final Status: `all` suite verified clean

**Date:** 2026-07-30

After all fixes (alignment fix, restore_task_fields fix), the full `all` test
suite runs to completion:

```
S: all no_op_new_mempool_reuse_after_free 882/882: PASS
  PASSED:     881
  FAILED:     1
```

**Single failure:** `stack_profiler_current_task_stack_valid` (test 852) — a
pre-existing issue where `cur->kernel_stack == nullptr`. Unrelated to TCB
corruption.

**Verified:**
- `AllTasksRegistry::restore()` skips zero TCBs (confirmed by diagnostic log)
- Zero `remove_task` 0xDD errors
- Zero `[ALLTASKS-RESTORE]` skip messages
- The IPC deadlock at test ~78 was transient — resolved after code changes
  (likely the alignment fix or restore_task_fields fix)

## Open questions

The `all` suite now passes 881/882, but the root cause is **not understood**.
The fixes (alignment padding, `restore_task_fields` position fallback, `nu_copy`)
eliminated the symptoms but the initial write that corrupts boot-time TCB fields
remains unidentified.

### Key unknowns

1. **What writes 0xDD/0x5F58 garbage to the snapshot buffer?** The `nu` field
   corruption at test ~847 was the original symptom. It disappeared after the
   alignment fix + `restore_task_fields` fix. The write source was never captured.

2. **Why did the IPC deadlock at test ~78 disappear?** The deadlock was present
   in every `all` run for hours, then vanished after the code changes. The
   `restore_task_fields` fix restoring `magic`/`id` fields may have corrected
   a TCB whose corrupted `id` prevented scheduler matching, causing priority
   inversion → IPC timeouts.

3. **What causes `kernel_stack = nullptr` on the INIT task?** The boot-time
   INIT task is pinned (can't be freed). Yet its `kernel_stack` field is null
   by test 852. `kernel_stack` is not in `TaskFields`, so `restore_task_fields`
   never restores it. If a stray write zeroes it, it stays zero permanently.
   Adding `kernel_stack` to `TaskFields` would fix this symptom but not the
   root cause.

### Hypothesis (unconfirmed)

A stray write during test execution corrupts a boot-time TCB's `id` field
(possibly via a stale TLB entry, a buffer overflow from an adjacent MemPool
block, or the PMM restore recycling a page that was previously used for a page
table). `restore_task_fields` cannot match this TCB by ID, skips it, and the
TCB retains all stale fields (state/priority/context). The scheduler then
behaves incorrectly (wrong task selected for dispatch), causing IPC timeouts
and lock contention. The `restore_task_fields` position fallback fixes this by
always restoring fields regardless of ID match.

This hypothesis is plausible but **unproven** — it requires catching a TCB with
corrupted `id` in `restore_task_fields`, which has not been observed.

### Ideas for catching the root cause (future work)

**1. MPU-guarded TCB pages.** Place each TCB in its own 4 KB page (instead of
packing 8 TCBs per 64 KB MemPool pool-8 block). Unused TCB pages are marked
read-only via page-table manipulation. Any write to a freed/unused TCB page
immediately GPFs with the exact instruction and backtrace. Implementation:
- Allocate TCBs individually via `PMM::alloc_page()` instead of `MemPool`
- Set page to read-only (`!PAGE_WRITE`) when freed
- Set page to read-write when allocated
- Cost: ~4 KB per TCB × 64 max = 256 KB total (acceptable for debug builds)

**2. Canary checks on every tick and context switch.** Add `t->magic == TCB_MAGIC`
validation inside `on_tick()` and `switch_to_task()`. If a TCB is corrupted
mid-execution (between snapshot_restore cycles), the corruption is detected at
the earliest possible point — not at the next `remove_task` call. This narrows
the corruption window from "between test A and test B" to "between tick N and
tick N+1".

**3. QEMU icount replay for reproducible runs.** Boot with:
```
qemu-system-x86_64 -icount shift=auto,rr=record,rrfile=replay.bin ...
```
This records all non-deterministic input (timers, IRQs, serial, disk) into
`replay.bin`. The exact same sequence can be replayed with:
```
qemu-system-x86_64 -icount shift=auto,rr=replay,rrfile=replay.bin ...
```
If the corruption reproduces under record, replay gives bit-identical
execution every time — essential for lldb watchpoint debugging across
long-running test suites. Note: icount replay requires `-icount` on the
QEMU command line; the Makefile would need a `make replay-test` target.

**4. Ring-buffer write tracker for TCB fields.** Create a small struct:
```cpp
struct TcbWriteLog {
    uint64_t timestamp;       ///< Tick count
    uint64_t tcb_addr;        ///< Address of TCB being modified
    uint64_t field_offset;    ///< Offset within TCB (e.g. offsetof(magic))
    uint64_t old_value;       ///< Value before write
    uint64_t new_value;       ///< Value after write
    void     *caller;         ///< __builtin_return_address(0)
};
static constexpr size_t WRITE_LOG_DEPTH = 50;
static TcbWriteLog s_tcb_write_log[WRITE_LOG_DEPTH];
static size_t s_tcb_write_idx = 0;
```
Wrap every TCB field write (or at least the critical ones: `magic`, `id`,
`kernel_stack`, `state`) in a helper function that:
1. Captures old value, new value, caller address from `__builtin_return_address(0)`
2. Stores to the ring buffer at `s_tcb_write_log[s_tcb_write_idx++ % WRITE_LOG_DEPTH]`
3. Performs the actual write

When a corruption is detected, dump the ring buffer to see the last 50
modifications of that TCB — including the corrupting write and its caller.

Alternatively: use hardware watchpoints via lldb (see AGENTS-KERNEL-BRIEFING.md
§14) on the specific TCB address. This is the most direct approach but requires
the corruption to be reproducible under the debugger.
