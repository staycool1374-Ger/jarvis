[>] Running Agent 3: Kernel Synthesizer...
# Jarvis RTOS — Kernel Audit Report: `src/kernel/task/*`
**Auditor:** Lead Kernel Architect
**Disposition:** Attacker report reviewed against source. Several findings are confirmed defects; several are filtered as false positives or out-of-scope speculation (see Appendix). Confirmed findings below are formatted for direct consumption by the fix-implementation agent.

---

## Confirmed Findings

- [ ] **JRVS-SCHED-001 — Unbounded linear probe in task-ID hash table**
  **FUNCTION:** `Scheduler::id_table_insert()` — `src/kernel/task/scheduler.cpp`
  **ROOT CAUSE:** The open-addressing insert loop `while (id_table_[idx] != nullptr && id_table_[idx] != ID_TOMBSTONE) { idx = (idx + 1) & ID_TABLE_MASK; }` has no iteration bound and no full-table check. The compiler's own infinite-loop analyzer warning is explicitly suppressed via `#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"` instead of being resolved. If the table (`ID_TABLE_SIZE = 2 * CONFIG_MAX_TASKS`) is ever full of live (non-tombstone) entries, this spins forever inside a function called from every `add_task`/`register_task`/`add_task_err`. This is non-WCET-analyzable and unacceptable for ASIL-D.
  **REQUIRED FIX:**
  1. Remove the `#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"` suppression entirely — it must never be used to silence a real defect.
  2. Bound the probe loop with an explicit `for (uint64_t probes = 0; probes < ID_TABLE_SIZE; ++probes)` counter (mirrors the pattern already correctly used in `id_table_remove`/`id_table_find`).
  3. On exhausting `ID_TABLE_SIZE` probes without finding a free/tombstone slot, return a `bool`/`errors::SchedulerError` (`SCHED_ERR_TABLE_FULL`) instead of `void`; update the signature of `id_table_insert` and propagate the failure to `register_task`, `add_task`, and `add_task_err` (the latter already has an error-return convention — reuse it).
  4. No dynamic allocation permitted; this is a pure bounds-check fix on the existing fixed-size array.

---

- [ ] **JRVS-SCHED-002 — Missing guard page on 3 of 4 kernel-stack allocation paths**
  **FUNCTIONS:**
  - `TaskControlBlock::create()`, test-active fallback branch (`use_window == false` path, label `done_stack:` falls through to `stack_virt = arch::HHDM_OFFSET + stack_phys;`) — `src/kernel/task/task.cpp`
  - `TaskControlBlock::create_user()` — kernel stack setup: `kstack_virt = arch::HHDM_OFFSET + kstack_phys;` — `src/kernel/task/task.cpp`
  - `TaskControlBlock::clone()` — kernel stack setup: both the `is_user_task` and non-user branches assign `tcb->kernel_stack` directly from HHDM/physical address with no guard page — `src/kernel/task/task.cpp`
  **ROOT CAUSE:** The HHDM (`arch::HHDM_OFFSET`) linearly maps *all* physical RAM, so the pages immediately adjacent to a kernel stack allocated via HHDM are themselves valid, mapped memory. A kernel-stack overflow on any task created through these three paths silently corrupts adjacent kernel/heap data instead of trapping — a direct violation of the mandatory guard-page rule for a safety-critical RTOS. Only the primary path in `create()` (`use_window == true`, using `alloc_kslot()`) maps a genuine unmapped guard page below the stack.
  **REQUIRED FIX:**
  1. Extend the existing `alloc_kslot()` / `map_kstack_page()` / `free_kslot()` machinery (already implemented, zero-allocation, fixed-pool-backed) to be the *single* kernel-stack provisioning path for `create()` (test-active case), `create_user()`, and `clone()` — remove the HHDM-direct-mapping fallback for kernel stacks entirely, or, if HHDM must be retained for the test-isolation case, explicitly `VMM::unmap_page` (not merely leave unmapped) one page immediately below `stack_virt` before use, and re-map it (or refuse re-use) on `cleanup()`.
  2. In `cleanup()`, the corresponding `unmap_kstack_page()` / `free_kslot()` calls must be invoked unconditionally whenever a guard page was installed, mirroring the existing `kstack_slot_va_`-gated block — extend that gating so it also covers stacks from `create_user()`/`clone()`.
  3. No heap allocation: reuse the existing fixed `KSlotEntry` pool (`KSLOT_POOL_SIZE = 64`) and the pre-mapped `s_kstack_pt_pages` page tables already present in the file; do not introduce a new allocator.
  4. Add a static assertion or boot-time check that `KSLOT_POOL_SIZE >= CONFIG_MAX_TASKS` so the guarded-window allocator can never silently fall back to an unguarded mapping under normal task counts.

---

- [ ] **JRVS-SCHED-003 — Non-RAII manual lock/unlock discipline on `scheduler_lock_`**
  **FUNCTIONS:** `Scheduler::on_tick()`, `Scheduler::rate_monotonic_schedule()`, `Scheduler::switch_away_from_terminating()`, `Scheduler::unregister_task()` — `src/kernel/task/scheduler.cpp`
  **ROOT CAUSE:** These four functions manually call `scheduler_lock_.try_lock()`/`.lock()`/`.unlock()` with multiple early-return statements that each must remember to unlock first (e.g. `switch_away_from_terminating()` has three separate `unlock(); return;` pairs). Elsewhere in the same file (`terminate()`, `register_table_ops`, `add_task()`) RAII `SpinLockGuard<sync::SpinLock>` is used correctly. This inconsistency is a certified maintenance hazard: any future edit that adds a new early return without the matching `unlock()` call introduces a silent permanent lock-hold (system-wide scheduler deadlock) that no test may catch. ASIL-D requires deterministic, audit-provable lock discipline, not "the current code happens to unlock on every path."
  **REQUIRED FIX:**
  1. Convert all four functions to use `SpinLockGuard<sync::SpinLock>` (already defined in `kernel/sync/spinlock_guard.hpp` and used elsewhere in this file) instead of manual `.lock()/.unlock()`.
  2. For the `try_lock()` cases (`on_tick()`, `rate_monotonic_schedule()`, `unregister_task()`), use a guard variant that supports "acquired-or-not" semantics (e.g. construct `SpinLockGuard` with a `std::defer_lock`-equivalent tag and call `guard.try_lock()`, checking `guard.owns_lock()`), so the destructor unconditionally releases on any return path, including ones added later.
  3. For `switch_away_from_terminating()`'s pattern of "unlock before the IRQ-guarded publish step," restructure so the guard's scope ends explicitly at the point of publish (`{ SpinLockGuard<sync::SpinLock> guard(scheduler_lock_); ... }` followed by the un-guarded IRQ-disabled publish section), rather than manual unlock calls interleaved with logic.
  4. Zero dynamic allocation; this is a pure RAII-wrapper substitution with no behavioral or performance change on the fast path.

---

- [ ] **JRVS-SCHED-004 — Divergent IrqGuard headers included across the module**
  **FUNCTION/FILES:** `#include <kernel/arch/irq_guard.hpp>` in `src/kernel/task/task.cpp` vs. `#include <kernel/arch/hal/irq_guard.hpp>` in `src/kernel/task/scheduler.cpp` and `src/kernel/task/taskdefs.cpp`.
  **ROOT CAUSE:** Two distinct header paths for the same `arch::IrqGuard` RAII type are included in different translation units of the same module. Whether or not they currently resolve to the same underlying type, this is architectural drift with no single source of truth — a change to one header silently desyncs from the other, and there is no compile-time guarantee they stay identical. This is a certification blocker for module traceability (each safety-relevant primitive must have exactly one canonical definition path).
  **REQUIRED FIX:**
  1. Determine the canonical location (recommend `kernel/arch/hal/irq_guard.hpp`, since it is used by the majority of the module including the scheduler core).
  2. Update `src/kernel/task/task.cpp` to include the canonical header only.
  3. If `kernel/arch/irq_guard.hpp` is a legacy/duplicate file, delete it and grep the full source tree for remaining includes of the old path, fixing each.
  4. Add a `static_assert(std::is_same_v<arch::IrqGuard, arch::IrqGuard>, ...)`-style compile guard is not meaningful here — instead, add a `#error "Do not include this legacy header — use kernel/arch/hal/irq_guard.hpp"` stub in the deprecated file until all includes are migrated, then delete it.

---

- [ ] **JRVS-SCHED-005 — O(n) worst-case fallback in registry/queue removal breaks O(1) scheduling guarantee**
  **FUNCTIONS:** `AllTasksRegistry::remove()` — `src/kernel/task/all_tasks_registry.cpp`; `ReadyQueueManager::remove()` — `src/kernel/task/ready_queue_manager.cpp`
  **ROOT CAUSE:** Both functions cannot trust `t->priority` / `tcb.rq_priority_` to locate the bucket a task actually lives in, because priority-inheritance/sporadic-server replenishment changes effective priority without re-indexing the node. The current fix is a linear scan: `AllTasksRegistry::remove()` walks **every** priority bucket (`for (uint64_t p = 0; p < NUM_PRIORITIES; ++p)`) until it finds the node; `ReadyQueueManager::remove()` falls back to scanning **every** priority queue (`for (uint64_t p = 0; p <= CONFIG_PRIORITY_CEILING; ++p)`) when `rq_priority_` doesn't match. Because a PI-boost/replenishment-triggered stale index is a *routine*, not exceptional, occurrence, this O(NUM_PRIORITIES × depth) fallback executes on a normal hot path (`Scheduler::terminate`, `dequeue_ready`, `release_zombie`), invalidating any WCET bound claimed for an "O(1) bitmap scheduler."
  **REQUIRED FIX:**
  1. Add a single `uint64_t current_bucket_` field to `TaskControlBlock` (task.hpp) that is the **sole authoritative record** of which priority bucket a task is physically linked into, in **both** `AllTasksRegistry` and `ReadyQueueManager`.
  2. Every insertion path (`AllTasksRegistry::append`, `ReadyQueueManager::enqueue`) must set `t->current_bucket_ = prio;` at insertion time.
  3. Every priority-change path that calls `move_priority()` (scheduler.cpp: `Scheduler::move_priority`, deadline-miss demote, sporadic-server replenishment path in `on_tick()`) must update `t->current_bucket_` atomically with the move.
  4. Rewrite `AllTasksRegistry::remove()` and `ReadyQueueManager::remove()` to index directly via `t->current_bucket_` — O(1), no scanning. Retain the existing `safe_tcb`/`is_valid` guard on the neighbor-pointer unlink logic (that part is correct and must stay), but delete the priority-bucket search loops entirely.
  5. Zero dynamic allocation: this is one added `uint64_t` field per TCB (already fixed-size, pool-allocated) plus O(1) index rewrites.

---

- [ ] **JRVS-SCHED-006 — O(n²) worst-case nested scan in `reap_orphans()` invoked from ISR-adjacent tick path**
  **FUNCTION:** `Scheduler::reap_orphans()` — `src/kernel/task/scheduler.cpp`
  **ROOT CAUSE:** For every candidate terminated task `t` in the outer `all_tasks_` scan (O(n)), the function performs (a) a `TaskIter` scan over **all** tasks to adopt orphaned children whose `parent_id == t->id`, and (b) a second full `TaskIter` scan to check for a `page_table_shared_` child. Both inner scans are O(n), yielding O(n²) total, executed unconditionally every 100 ticks from `on_tick()` (`if (tick_counter % 100 == 0) { if (!s_test_active_) reap_orphans(); ... }`), i.e., directly on the timer-interrupt-driven scheduling path. This is not WCET-bounded and violates the O(1)/deterministic dispatch requirement for this scheduler module.
  **REQUIRED FIX:**
  1. Maintain the existing intrusive `first_child` / `next_sibling` / `prev_sibling` list (already present on `TaskControlBlock`) as the **sole** mechanism for child enumeration — remove the redundant `TaskIter` full-table scan in the "adopt remaining children" block (`for (TaskIter it(0);;) { ... if (c->parent_id == t->id) ... }`); the intrusive child list already gives O(children of t) directly via `t->first_child`, making the second scan strictly redundant with the first block above it in the same function.
  2. For the `page_table_shared_` sharing-child check, do not scan all tasks — track a `uint64_t sharing_child_count_` counter on the parent `TaskControlBlock`, incremented in `clone()`/`create_user()` when `page_table_shared_` is set on a child referencing this parent, and decremented in `cleanup()`. The reaper then checks `t->sharing_child_count_ == 0` in O(1).
  3. Cap `reap_orphans()`'s total work per invocation at `MAX_REAP` (already present) and ensure the (now O(1)-per-task) adoption/sharing checks keep the whole function at O(MAX_REAP), independent of total task count.
  4. Zero dynamic allocation: reuse existing intrusive pointers plus one new fixed `uint64_t` counter field.

---

- [ ] **JRVS-SCHED-007 — TCB lifetime/ownership modeled via raw pointers + scattered runtime validity guards instead of enforced reference safety**
  **FUNCTIONS/FILES (representative, non-exhaustive):**
  `switch_to_task()`, `Scheduler::next_task()`, `Scheduler::set_current()` — `src/kernel/task/scheduler.cpp`;
  `AllTasksRegistry::append/remove/first/next` — `src/kernel/task/all_tasks_registry.cpp/.hpp`;
  `DeadlineList::insert/remove` — `src/kernel/task/deadline_list.cpp/.hpp`;
  supporting evidence: `safe_tcb()` (all_tasks_registry.cpp), `TaskControlBlock::is_valid()` (task.hpp), `is_poisoned_block()` (scheduler.cpp), `debug_check_tcb_reuse()` (task.cpp).
  **ROOT CAUSE:** Every one of these APIs accepts/returns `TaskControlBlock*` with no compile-time non-null or lifetime contract, forcing every call site to defensively re-verify liveness via address-range + magic-number checks before dereference. The presence of `is_poisoned_block()` (detecting the `0xDD` MemPool free-poison pattern inside `on_tick()`'s sporadic-server loop) and `debug_check_tcb_reuse()` (an entire function dedicated to detecting a freed TCB still aliased by a live pointer) is direct evidence that use-after-free on TCB pointers is a recurring, previously-exploited defect class (documented inline as `BUGS.md#019/#020`), not a theoretical concern. This drives correctness *and* WCET risk (Finding JRVS-SCHED-008 below is a direct downstream symptom).
  **REQUIRED FIX (scoped, incremental — do not attempt a whole-module rewrite in one pass):**
  1. For every registry/list API where the argument is **never** legitimately null at the call site today (`AllTasksRegistry::append`, `remove`; `DeadlineList::insert`, `remove`; `switch_to_task`'s `next` parameter), change the parameter type from `TaskControlBlock*` to `TaskControlBlock&`. This eliminates an entire class of "was it null or dangling" ambiguity at the type level and is a compile-time-checked, zero-runtime-cost change.
  2. Retain `TaskControlBlock*` **only** for genuinely-nullable outputs (`Scheduler::find_task`, `AllTasksRegistry::first_ptr/next_ptr`, `Scheduler::current_task`) — document this as the single allowed pointer convention in a header comment on `task_fwd.hpp`.
  3. Keep the existing `is_valid()`/`safe_tcb()` guards **only** at true trust boundaries (deserializing a snapshot, walking a link that crosses an ISR-vs-task-context race) — do not remove them where they defend against real concurrent-corruption scenarios (e.g. snapshot restore). The reference-typed conversion in step 1 removes the *need* for defensive checks on internally-owned, freshly-verified pointers, not on cross-boundary data.
  4. `is_poisoned_block()` must be deleted once JRVS-SCHED-005's `current_bucket_`-indexed removal and a proper `sporadic_server` ownership contract (unique, non-shared, freed exactly once in `cleanup()`) are enforced; until that refactor lands, leave `is_poisoned_block()` in place as an interim safety net, but file it as a tracked-debt item, not a permanent fixture.
  5. No dynamic allocation involved; this is purely a signature/type-safety change plus deletion of now-redundant runtime checks at call sites that were converted to references.

---

- [ ] **JRVS-SCHED-008 — Context-switch hot path (`switch_to_task`) carries non-deterministic defensive overhead caused by JRVS-SCHED-007**
  **FUNCTION:** `switch_to_task()` — `src/kernel/task/scheduler.cpp`
  **ROOT CAUSE:** The single most frequently executed function in the kernel performs, per call: an `effective_priority()`-style poison check, a conditional (release) / unconditional (`CONFIG_DEBUG`) O(n) "resolve true RSP owner" scan over all tasks to compensate for `current_task_ptr_` drift, and manual dual-ordering iret-frame validation. Each of these is a direct patch for the ownership/lifetime problems in JRVS-SCHED-007, not an intentional, WCET-budgeted design element.
  **REQUIRED FIX:**
  1. Do not attempt to remove this instrumentation until JRVS-SCHED-007's reference-safety pass is complete for `current_task_ptr_`'s producers (`Scheduler::set_current`, `scheduler_on_context_switch`) — remove the "owner resolution" scan only after `current_task_ptr_` is guaranteed by construction (via the new reference-typed APIs) to never drift onto a peer TCB.
  2. Once that invariant is enforced, delete the `CONFIG_DEBUG` unconditional O(n) scan and the release-mode conditional scan in `switch_to_task()`, replacing both with a direct `debug-assert(current == <expected owner>)` check with zero runtime cost in release builds.
  3. Track this as a follow-up ticket gated on JRVS-SCHED-007 landing — do not schedule it standalone, to avoid re-introducing the corruption it currently guards against.

---

## Appendix — Findings Filtered as False Positives / Out of Scope

| Attacker Claim | Disposition | Reason |
|---|---|---|
| "`arch::IrqGuard` used as a locking primitive is a forbidden anti-pattern; replace with `SpinLock`" | **FILTERED (partially false positive)** | This is a single-core (UP) kernel (explicitly documented: `"Single-core UP: read-only ready-queue peek needs only IRQ-safety"`). Disabling IRQs to protect a data structure from ISR-vs-task-context races is the *correct and necessary* pattern on UP hardware — a plain `SpinLock` alone would deadlock the ISR against a task it preempted mid-critical-section. The legitimate sub-issues (non-RAII discipline, duplicate headers) are retained as JRVS-SCHED-003/004. |
| "`next_task()`'s ready-queue peek loop is unbounded / could spin forever on corrupted state" | **FILTERED** | The loop is bounded by `ready_queue_.dequeue_highest()` unconditionally removing an entry on every iteration that doesn't return; total iterations ≤ ready-queue size, which is bounded by `CONFIG_MAX_TASKS`. No corruption path in the reviewed code re-inserts a dequeued node into the same scan. Speculative, not demonstrated. |
| "`dmesg_task_main()`'s inner busy-wait spin is priority-inversion-prone" | **FILTERED (informational only)** | Real, but not a scheduler-safety defect — it's a throttle in a background logging task with no WCET claim attached in this review. Not actionable without a stated budget requirement; no fix directive issued. |
| "`hal::bits::find_highest_bit()` zero-input UB must be audited" | **FILTERED — out of scope** | `bits.hpp` implementation was not included in the reviewed file set; `PriorityMap` call sites already correctly guard against zero input. No verifiable defect in the provided code. |
| "PCP/PIP conflation in `TaskControlBlock` is unverifiable/ambiguous" | **FILTERED — out of scope** | `sync::Mutex` implementation was not included in the reviewed file set; the `held_ceilings_`/`waiting_on_mutex` fields alone do not constitute a demonstrated defect. Requires a follow-up audit pass once `mutex.cpp`/`mutex.hpp` are provided. |
| "O(n³) in `reap_orphans()`" | **CORRECTED, not dropped** | Actual complexity is O(n²) (two independent O(n) inner scans per outer O(n) iteration, not compounded into O(n³)). Retained as JRVS-SCHED-006 with corrected complexity characterization. |
