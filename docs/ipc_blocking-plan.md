# ipc_blocking — Implementation Plan: runq-membership + deferred-trigger freeze

## Status of prior change (this session)
`scheduler.cpp` now assigns `current_task_ptr_ = next` **synchronously** in
`switch_to_task` (lifecycle-synchronous ownership, per `docs/
task-lifecycle-review.md` PART D). The ISR's `scheduler_on_context_switch` no
longer reassigns it. This is directionally correct but **insufficient** and
introduces a new lag direction (see Side Effects). The 1 run after the change
ended in `[STALL] HALT` with a READY task T0 stranded outside the runq.

## Root-cause of the observed STALL (the freeze, not the IPC lag)
Two cooperating defects:

### D1 — Frozen deferred-switch trigger permanently blocks rescheduling
`scheduler.cpp:1578` `rate_monotonic_schedule()` early-returns whenever
`scheduler_save_rsp_to != 0`:
```
if (__atomic_load_n(&scheduler_save_rsp_to, ...) != 0) { unlock; return; }
```
This means: while a deferred switch is *pending* (published by
`switch_to_task`, awaiting the timer ISR), **no further `next_task()` is ever
called** — including the `next_task()` lazy-rebuild that heals a stranded READY
task. If the published switch is never consumed by the ISR (ISR skipped due to
`isr_nesting_depth > 2` at `isr_stubs.asm:112`, or the switch was defused but
the replacement never published, or the ISR wedged), `scheduler_save_rsp_to`
stays set forever → `rate_monotonic_schedule` returns every tick → scheduler
frozen → a READY task (T0 in the trace) is never healed → STALL.

This is the dominant freeze cause for the `[STALL] ticks_since_switch=301` with
`bm_lo=0` and a READY `inrq=0` task present.

### D2 — READY task can be left dequeued + not re-enqueued (INV-2 / VIOL-5)
`next_task()` (scheduler.cpp:395) `dequeue_highest()`s `next`, then
`switch_to_task` may hit the dispatch-guard (1467-1482) and `return` after
setting `next->in_ready_queue_ = false` **without re-enqueueing**. The guard is
correct for a genuinely-running `next`, but once a non-running READY task is
dequeued and the guard's `next_is_runner`/`next_is_running` mis-fires, the task
is stranded (READY + `inrq=0` + not in any bucket). The lazy rebuild only runs
when `dequeue_highest` returns null, so a stranded task is invisible until the
whole RQ drains.

## Plan (ordered, minimal, doc-aligned)

### Step 1 — Fix D1: never let a pending trigger freeze rescheduling
Make `rate_monotonic_schedule` (and `reschedule`) **consume/clear a stale
pending trigger** instead of bailing. Concretely, at scheduler.cpp:1578 replace
the early-return with: if a trigger is pending, clear the deferred-switch
atomics (`scheduler_save_rsp_to=nullptr`, `scheduler_load_rsp_from=0`,
`scheduler_load_cr3_from=0`, `scheduler_next_task_id=UINT64_MAX`) and
**continue** to compute `next_task()` so the scheduler stays live. The dropped
switch is superseded by the fresh `next_task()` selection anyway.

Caveat: clearing the trigger loses one pending switch, but `next_task()`
re-selects the correct target immediately, so no task is starved. This mirrors
the (B) defuse logic already in `switch_to_task` (1506-1504) and `reschedule`
(1624-1629) — apply the same at the per-tick gate.

### Step 2 — Fix D2: guarantee runq membership invariant (INV-2)
In `next_task()`'s dispatch-guard drop path (1467-1482): when the guard decides
NOT to dispatch `next` (because it is the physical runner / already RUNNING),
**re-enqueue `next` if it is not the current running task and not idle** — i.e.
only skip re-enqueue for the truly-running task. For any other READY task that
was dequeued but not dispatched, call `enqueue_ready(*next)` before returning so
it is never stranded.

Alternatively (defensive, doc PART J): have `next_task()`'s lazy rebuild also
run when the selected `next` was dropped — but the targeted re-enqueue is
cheaper and directly closes INV-2.

### Step 3 — Re-evaluate the synchronous `current_task_ptr_` change (Side Effects)
With Step 1/2 the scheduler no longer freezes, but the synchronous assignment
(scheduler.cpp ~1545) creates a **new lag direction**: `current_task_ptr_`
points at `next` *before* the ISR applies `next`'s RSP. Any `current_task()`
reader (IPC::recv/send at ipc.cpp:166,212,227,245) between publish and ISR sees
`next` while the CPU is still on the old task's stack → routes IPC to `next`'s
queue for a not-yet-running task. This is the inverse of the old "lag behind"
bug.

Resolution options (choose one):
  (a) Keep synchronous assignment but make `current_task()` RSP-authoritative
      (the earlier proposed RSP-owner resolution) so it always returns the
      PHYSICAL runner regardless of `current_task_ptr_`. This kills both lag
      directions. Cost: O(n) per `current_task()` call (debug/test kernel OK).
  (b) Revert the synchronous assignment and instead keep `current_task_ptr_`
      assigned by the ISR (original behavior), relying on Steps 1+2 to keep the
      ISR applying switches promptly so the lag window is bounded.

Recommend (a): it makes `current_task()` derive truth from the stack (the only
unforgeable signal of "who is running"), consistent with the heal logic already
at scheduler.cpp:1366-1389, and eliminates lag in both directions. IPC routing
becomes correct by construction.

### Step 4 — Snapshot/restore compatibility (doc PART I)
The synchronous/`current_task()` change must not break snapshot restore
(restore identifies current by RSP match, PART I.3 step 11). With (a),
`current_task()` returns the RSP owner, which is exactly what restore computes
— so they agree. No change to test_isolate.cpp needed (confirmed by doc PART I.5
H3/H4: addresses are stable; magic valid through teardown per D5). Validate that
`snapshot_restore` still re-identifies current correctly.

### Step 5 — Validation (mandatory per AGENTS / doc PART E)
Run, recording every row in test-history.txt:
  - `make execute-test x86_64 debug ipc_blocking` ×12 (target 0 HANG 0 FAIL)
  - `make execute-test x86_64 debug all` (regression: the `all` suite must stay
    green — the guard/no-op and rebuild were added to keep it alive)
  - `make execute-test x86_64 debug test_task_lifecycle` (INV-1 / reuse-UAF gate)
  - `make execute-test x86_64 debug test_ipc_robustness` (VIOL-2 / VIOL-4 window)
After each: confirm ResourceTracker no leak delta (snapshot check).

## Side-effect matrix
| Change | Fixes | Risk / Side-effect | Mitigation |
|---|---|---|---|
| Step 1 clear stale trigger per-tick | D1 freeze; unblocks heal | drops one pending switch | next_task() re-selects immediately; no starvation |
| Step 2 re-enqueue dropped READY | D2 strand (INV-2) | extra enqueue of a task that may be current | guard: only re-enqueue if `next != current && next != idle_task_` |
| Step 3a RSP-authoritative current_task() | both lag directions | O(n) per call | acceptable on debug/test single-core kernel; cache-free |
| Step 3b revert sync assign | simpler | restores old lag-behind window | relies on Step 1 prompt ISR apply |
| My synchronous assign (keep) | lifecycle ownership | new lead-lag + IPC mis-route window | resolved by Step 3a |

## Rollback
If `all` or `test_task_lifecycle` regresses, revert the synchronous
`current_task_ptr_` assignment (scheduler.cpp ~1545 + scheduler_on_context_switch
restore) first; Steps 1+2 are independent and safe to keep. Each step committed
separately so bisect is clean.

## Files touched
- src/kernel/task/scheduler.cpp: rate_monotonic_schedule (1578), reschedule
  (1624), dispatch-guard drop (1467-1482), switch_to_task sync assign (~1545),
  scheduler_on_context_switch (2160), current_task() (308).
- src/kernel/task/scheduler.hpp: set_current_physical (added this session;
  keep), current_task() signature unchanged.
- No change to test_isolate.cpp / isr_stubs.asm expected.
