# RMS Rework — Rate-Monotonic Schedule Lock Contention Fix

## Problem

The `ipc_kernel_block_skips_sti` test hangs because `reschedule()` and
`rate_monotonic_schedule()` contend on `scheduler_lock_`. The harness spins
in `while (state != TERMINATED) { Scheduler::reschedule(); }`, which takes
`scheduler_lock_` every iteration. Between iterations, the timer ISR fires
and `rate_monotonic_schedule()` calls `scheduler_lock_.try_lock()` which
fails because the harness holds the lock → no scheduling happens → hang.

## Root Cause

Two architectural issues:

**A) Lock granularity** — `scheduler_lock_` guards ALL scheduler state. A
read-only peek in `reschedule()` takes the same lock as the write path in
`rate_monotonic_schedule()`/`switch_to_task()`. This is unnecessarily coarse:
the peek needs only IRQ-safety (prevent ISR from mutating the queue), not
the full spinlock.

**B) Synchronous reschedule request** — `reschedule()` is called from task
context (IPC paths, yield, etc.) but its only meaningful action is to set
`scheduler_need_resched = true`. The lock is taken only to peek the ready
queue — a read-only operation that confirms "yes, there IS a higher-priority
task waiting." This confirmation is informative but not strictly required:
setting `need_resched` unconditionally is safe (the next tick checks anyway).

## Design

### Principle: Split Read-Only Requests from Read-Write Scheduling

The scheduler has two distinct access patterns:

| Path | Access | Protection needed |
|---|---|---|
| `next_task()` | Read-write (dequeue, modify TCB) | `scheduler_lock_` |
| `switch_to_task()` | Read-write (publish atoms, modify TCB) | `scheduler_lock_` |
| `rate_monotonic_schedule()` | Read-write (call next_task, switch) | `scheduler_lock_` via `try_lock()` |
| `reschedule()` | Read-only (peek queue, set flag) | IRQ-disable only (UP) |
| `set_task_ready()` | Read-write (enqueue, set state) | `scheduler_lock_` + `IrqGuard` |

For single-core UP, read-only accesses to scheduler data can be protected by
`IrqGuard` instead of the full spinlock. This prevents lock contention with
the timer ISR.

## Phase 1 — Lock-Free `reschedule()`

### Change

Replace `scheduler_lock_` with `arch::IrqGuard` in `reschedule()`:

```cpp
void Scheduler::reschedule() noexcept {
    // Single-core UP: read-only peek needs only IRQ-safety, not the full
    // scheduler_lock_.  Holding the lock here prevents the timer ISR from
    // applying deferred switches via try_lock().
    arch::IrqGuard irq_guard{};

    if (all_tasks_.size() <= 1)
        return;

    auto *current = current_task();
    if (!current || current->magic != TaskControlBlock::TCB_MAGIC)
        return;

    auto *next = ready_queue_.peek_highest();
    if (!next || next == current)
        return;
    if (next == idle_task_ && current->state == TaskState::RUNNING)
        return;
    if (next->state != TaskState::READY && next->state != TaskState::RUNNING)
        return;

    // IrqGuard destructor re-enables IRQs here.
    // The timer ISR can now fire and acquire scheduler_lock_ to apply the
    // deferred switch.
    __atomic_store_n(&kernel::scheduler_need_resched, true, __ATOMIC_RELEASE);
}
```

**Safety argument for UP:**
- `all_tasks_.size()` is a `constinit uint64_t` — the timer ISR increments it
  during `add_task()` under the lock. With IRQs disabled, the ISR cannot fire.
- `ready_queue_.peek_highest()` reads the PriorityMap bitmap and the queue
  head pointer. Both are written by the ISR under the lock. IRQs-disabled
  prevents concurrent writes.
- `next->state` is a TCB field. Same argument.
- The atomic store to `scheduler_need_resched` is a single 8-byte release
  store, safe outside the lock.

**For SMP (future):** Replace `IrqGuard` with a `reschedule_lock_` (separate
from `scheduler_lock_`) or use a seqlock/RCU for the ready queue peek.

### Impact

| Metric | Before | After |
|---|---|---|
| Lock hold time in reschedule | Full spinlock acquire+release (~100ns) | `IrqGuard` only (~5ns) |
| Timer ISR lock acquisition | Failed during reschedule loop | Always succeeds |
| `ipc_kernel_block_skips_sti` | Hangs | Passes |

## Phase 2 — Split scheduler_lock_ into Two Locks

### Problem

`scheduler_lock_` currently guards three things:
1. Ready queue mutations (enqueue/dequeue in ISR and task context)
2. `all_tasks_` mutations (add/remove tasks)
3. `id_table_` mutations

When `reschedule()` holds the lock (even briefly), it blocks ALL scheduler
operations in the timer ISR.

### Design

Split into two locks:

```
scheduler_lock_      — guards all_tasks_, id_table_, deadline_list_ (rare writes)
ready_queue_lock_    — guards ready_queue_ mutations only (frequent writes)
```

The `ready_queue_lock_` is a separate spinlock. The timer ISR tries it
first (via `try_lock()`). If it fails, the ISR can still process other
per-tick work (deadline detection, sporadic server replenishment) while
skipping only the dispatch decision.

### Ordering

Always acquire in order: `ready_queue_lock_` → `scheduler_lock_` (when both
are needed). This prevents circular wait.

### Diagrams

```
Current:
  reschedule()          → scheduler_lock_.lock()
  rate_monotonic_schedule() → scheduler_lock_.try_lock()  ← FAILS if held

After Phase 2:
  reschedule()          → ready_queue_lock_.lock()  ← always succeeds
  rate_monotonic_schedule() → ready_queue_lock_.try_lock()
                              → if OK: dispatch decision
                              → if FAIL: skip dispatch, do other tick work
                              → scheduler_lock_.try_lock() for deadline/etc.
```

### Files touched

| File | Change |
|---|---|
| `src/kernel/task/scheduler.hpp` | Add `ready_queue_lock_` member |
| `src/kernel/task/scheduler.cpp` | Use `ready_queue_lock_` in `reschedule()`, `next_task()`, enqueue/dequeue paths |
| `src/kernel/ipc/ipc.cpp` | `block_sender`/`wake_sender` use `ready_queue_lock_` for move_priority |

## Phase 3 — Priority-Ordered Wakeup for Blocked-Sender Chains

### Problem

Blocked-sender chains (A blocked sending to B, which is blocked sending to C)
need priority-ordered wakeup: when C receives, B wakes. But the current
`wake_sender()` wakes in FIFO order (oldest blocked sender first), not
priority order. Under deadline pressure, a low-priority sender can be woken
before a high-priority one, causing priority inversion.

### Design

Replace the FIFO blocked-senders list with a priority-ordered intrusive
priority queue. `block_sender()` inserts by priority (highest first).
`wake_sender()` pops the highest-priority sender.

### Files touched

| File | Change |
|---|---|
| `src/kernel/ipc/ipc.hpp` | Change `blocked_senders_head` to priority-ordered list |
| `src/kernel/ipc/ipc.cpp` | `block_sender` inserts by priority; `wake_sender` pops highest |
| `docs/scheduler-spec.md` | Update §5 with priority-ordered semantics |

## Phase 4 — Deadline-Pressure-Aware Dispatch

### Problem

`rate_monotonic_schedule()` uses strict priority ordering. Under deadline
pressure, a task with an imminent deadline should be preferred over a
higher-priority task with no deadline pressure. The current scheduler has
deadline miss detection (post-facto) but no deadline-aware scheduling
decision (pre-emptive).

### Design

Add a `deadline_rush` flag to `TaskControlBlock`:
- Set when a task's deadline is within `CONFIG_DEADLINE_RUSH_TICKS` (e.g. 5)
- Cleared after the task runs
- `needs_switch()` and `next_task()` check: if current has no rush but next
  does, preempt even at equal/lower priority

### Files touched

| File | Change |
|---|---|
| `src/kernel/task/task.hpp` | Add `deadline_rush_` field |
| `src/kernel/task/scheduler.cpp` | `needs_switch()` and `next_task()` check rush flag |
| `src/kernel/jarvis_config.h` | Add `CONFIG_DEADLINE_RUSH_TICKS` |

## Verification Plan

| Phase | Tests | Timeout |
|---|---|---|
| Phase 1 | `ipc_blocking` (4), `o1_scheduler` (20), `scheduler` (51), `ipc` (42) | 2 min each |
| Phase 2 | Full regression gate + `priority_inheritance` (11), `lock_protocol` (34) | 3 min each |
| Phase 3 | `ipc` (42 — blocked-sender tests), `ipc_blocking` (4) | 2 min |
| Phase 4 | `deadline_miss` (5), `deadline_recovery` (4), `wcet` (1) | 2 min |
| Final | `all` (881 tests) | 6 min |

Record each in test-history.txt.

## Files Summary

| File | Phase | Change |
|---|---|---|
| `src/kernel/task/scheduler.cpp` | 1 | `reschedule()`: lock → IrqGuard |
| `src/kernel/task/scheduler.hpp` | 2 | Add `ready_queue_lock_` |
| `src/kernel/task/scheduler.cpp` | 2 | Split lock usage |
| `src/kernel/ipc/ipc.hpp` | 3 | Priority-ordered blocked list |
| `src/kernel/ipc/ipc.cpp` | 3 | Priority insert in block_sender |
| `src/kernel/task/task.hpp` | 4 | `deadline_rush_` field |
| `src/kernel/jarvis_config.h` | 4 | `CONFIG_DEADLINE_RUSH_TICKS` |
