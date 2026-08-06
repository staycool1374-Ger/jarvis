# Task & Scheduler Audit — Fix Specification

**Audit Source:** `audits/task+scheduler_audit.md` (8 confirmed findings)  
**Status:** v0.3.6 Implementation Plan  
**Target:** Hard real-time compliance (ASIL-D / IEC 61508 SIL 4)

---

## 1. JRVS-SCHED-001 — Unbounded id_table_insert Probe (CRITICAL)

### Problem
`id_table_insert()` has an open-addressing probe loop with no bound. If the table is ever full of live entries, it spins forever. The compiler's infinite-loop warning is suppressed via `#pragma` instead of fixed.

### Fix
1. Remove the `#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"` suppression
2. Add `for (uint64_t probes = 0; probes < ID_TABLE_SIZE; ++probes)` counter
3. On exhaustion, return `SCHED_ERR_TABLE_FULL`; propagate to all callers

### Files
`src/kernel/task/scheduler.cpp`

### Risk
Low — pure bounds addition.

---

## 2. JRVS-SCHED-002 — Missing Guard Page on 3 of 4 Kernel-Stack Paths (HIGH)

### Problem
`create()` (test-active), `create_user()`, and `clone()` allocate kernel stacks via HHDM with no unmapped guard page below. A stack overflow silently corrupts adjacent data instead of trapping.

### Fix
Extend `alloc_kslot()` / `map_kstack_page()` / `free_kslot()` to be the single kernel-stack provisioning path for all three functions. Remove the HHDM-direct-mapping fallback.

### Steps
1. `create()` test-active path: route through `alloc_kslot()` instead of `HHDM_OFFSET + stack_phys`
2. `create_user()`: route through `alloc_kslot()` for kernel stack
3. `clone()`: route through `alloc_kslot()` for kernel stack
4. `cleanup()`: call `free_kslot()` for all paths
5. Add `static_assert(KSLOT_POOL_SIZE >= CONFIG_MAX_TASKS)`

### Files
`src/kernel/task/task.cpp`

### Risk
Medium — changes the stack layout for 3 allocation paths. Must verify no stack overflow on test boot.

---

## 3. JRVS-SCHED-003 — Non-RAII Lock Discipline (HIGH)

### Problem
`on_tick()`, `rate_monotonic_schedule()`, `switch_away_from_terminating()`, `unregister_task()` manually call `.lock()/.unlock()` with multiple early-return paths. Any future edit that adds a new return without unlocking causes a permanent deadlock.

### Fix
Convert all four to `SpinLockGuard<sync::SpinLock>`. For `try_lock()` usage, use a guard variant with `.owns_lock()` check. Scope the guard to end explicitly before the publish step in `switch_away_from_terminating()`.

### Dependency
Related to VULN-002 (SpinLock infrastructure). Can be done in parallel with separate guard types.

### Files
`src/kernel/task/scheduler.cpp`

---

## 4. JRVS-SCHED-004 — Divergent IrqGuard Includes (LOW)

### Problem
`task.cpp` includes `<kernel/arch/irq_guard.hpp>` while `scheduler.cpp` and `taskdefs.cpp` include `<kernel/arch/hal/irq_guard.hpp>`. Two distinct paths for the same type create certification traceability risk.

### Fix
1. Canonical path: `kernel/arch/hal/irq_guard.hpp`
2. Fix `task.cpp` to use canonical path
3. Add `#error` stub in legacy path until all consumers migrate

### Files
`src/kernel/task/task.cpp`, `src/kernel/arch/irq_guard.hpp`

---

## 5. JRVS-SCHED-005 — O(N) Fallback in Registry/Queue Removal (CRITICAL)

### Problem
`AllTasksRegistry::remove()` and `ReadyQueueManager::remove()` cannot trust `t->priority`/`rq_priority_` because PI/sporadic-server replenishment changes effective priority without re-indexing. Both fall back to a linear scan of ALL priority buckets — O(NUM_PRIORITIES × queue depth) on a routine hot path.

### Fix
1. Add `uint64_t current_bucket_` field to `TaskControlBlock` — the authoritative record of which priority bucket the task is physically linked into in both `AllTasksRegistry` and `ReadyQueueManager`
2. Every insertion sets `t->current_bucket_ = prio`
3. Every priority-change path (`move_priority()`, deadline-miss demote, SS replenishment) updates `t->current_bucket_` atomically with the move
4. Rewrite both `remove()` functions to index directly via `t->current_bucket_` — O(1)

### Dependency
None. Independent change.

### Files
`src/kernel/task/task.hpp`, `all_tasks_registry.cpp`, `ready_queue_manager.cpp`, `scheduler.cpp`

### Risk
Medium — adds a field to TCB, changes hot-path removal. Must verify no regression in scheduler tests.

---

## 6. JRVS-SCHED-006 — O(N²) reap_orphans ISR Path (HIGH)

### Problem
`reap_orphans()` runs every 100 ticks from `on_tick()`. For each terminated task, it does two O(n) inner scans: (a) adopt orphaned children via `TaskIter` full-table scan, (b) check for `page_table_shared_` child. Total O(n²) per 100 ticks.

### Fix
1. Replace the child-adoption `TaskIter` scan with the existing intrusive `first_child`/`next_sibling` list — O(children of t) directly
2. Replace the `page_table_shared_` full scan with a `uint64_t sharing_child_count_` counter on the parent TCB, incremented in `clone()`/`create_user()`, decremented in `cleanup()`

### Files
`src/kernel/task/task.hpp`, `scheduler.cpp`, `task.cpp`

---

## 7. JRVS-SCHED-007 — TCB Lifetime via Raw Pointers (HIGH)

### Problem
All APIs accept/return `TaskControlBlock*` with no compile-time lifetime contract. Every call site defensively re-validates via `is_valid()`, `safe_tcb()`, `is_poisoned_block()`, `debug_check_tcb_reuse()`. Use-after-free is a recurring defect class (BUGS.md#019/#020).

### Fix (incremental — not a rewrite)
1. Change non-nullable API parameters from `TaskControlBlock*` to `TaskControlBlock&`:
   - `AllTasksRegistry::append(TaskControlBlock&)`, `remove(TaskControlBlock&)`
   - `DeadlineList::insert(TaskControlBlock&)`, `remove(TaskControlBlock&)`
   - `switch_to_task(TaskControlBlock& next)`
2. Retain `TaskControlBlock*` only for genuinely-nullable outputs (`find_task`, `current_task()`)
3. Keep `is_valid()`/`safe_tcb()` at true trust boundaries only

### Dependency
Large refactor — schedule after SCHED-005 (which touches related code).

### Files
Multiple files across `src/kernel/task/`

---

## 8. JRVS-SCHED-008 — switch_to_task Overhead (MEDIUM)

### Problem
`switch_to_task()` performs poison checks, O(n) RSP-owner resolution scan, and frame validation. These compensate for SCHED-007's pointer problems.

### Fix
Gate on SCHED-007: after reference-safety is enforced, remove the `CONFIG_DEBUG` scan and release-mode conditional scan, replace with `debug-assert(current == expected_owner)`.

### Dependency
Depends on SCHED-007.

---

## Implementation Order for v0.3.6

```
Phase A — Independent (can be done in any order, alongside memory audit Phase 1):
  SCHED-001  id_table bounded probe          [CRITICAL]
  SCHED-002  Guard page for all stacks        [HIGH]
  SCHED-004  Divergent IrqGuard includes      [LOW]
  SCHED-005  O(1) priority bucket + removal   [CRITICAL]
  SCHED-006  O(n²) reap_orphans              [HIGH]

Phase B — Lock discipline (related to memory audit VULN-002):
  SCHED-003  RAII lock discipline             [HIGH]

Phase C — Large refactors (gate after Phase A):
  SCHED-007  TCB reference safety            [HIGH]
  SCHED-008  switch_to_task overhead         [MEDIUM]  (gated on SCHED-007)
```

Memory audit (VULN-*) and scheduler audit (SCHED-*) phases can run in parallel since they touch disjoint subsystems (memory vs scheduling), except VULN-002/SCHED-003 which both involve SpinLock discipline.
