# Phase 3 — Clear Stale Pending Switch in rate_monotonic_schedule

## Problem

When `rate_monotonic_schedule()` (the timer ISR's scheduler callout) finds a
deferred switch already published (`scheduler_save_rsp_to != 0`) AND the
current task is RUNNING/READY, it **bails entirely**:

```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    auto *cur = current_task();
    if (!cur || cur->state == TaskState::RUNNING ||
        cur->state == TaskState::READY) {
        scheduler_lock_.unlock();
        return;   // ← BAIL — no next_task() called this tick
    }
    // (BLOCKED current: override pending switch)
    ...
}
```

This means: while a deferred switch is pending but the ISR hasn't applied it
yet, NO further scheduling happens. If that pending switch is never consumed
(e.g. the ISR was suppressed by nesting depth, or the target task was freed),
the scheduler is **frozen** — `rate_monotonic_schedule()` returns every tick
without calling `next_task()`.

**Historical context:** This guard exists to prevent publishing a second switch
on top of an existing one (would produce a mismatched RSP/CR3 pair). But the
correct response is to **clear the stale trigger** and continue, not to freeze.

This also interacts with the Phase 1 lazy-rebuild removal: the old lazy rebuild
in `next_task()` was the only recovery mechanism for orphaned tasks. Without
it, a frozen scheduler window means orphans are never recovered at all.

## Fix

Replace the early-return with a full disarm of the four deferred-switch
atomics, then fall through to `next_task()`:

**Before** (line 1742–1751):
```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    auto *cur = current_task();
    if (!cur || cur->state == TaskState::RUNNING ||
        cur->state == TaskState::READY) {
        scheduler_lock_.unlock();
        return;
    }
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
}
```

**After**:
```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    // A pending switch exists from a previous tick.  Clear all four
    // deferred-switch atoms so we can publish a fresh one.  The dropped
    // switch is harmless — next_task() re-selects the correct target.
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_next_task_id, (uint64_t)-1, __ATOMIC_RELEASE);
    // Fall through to next_task() below.
}
```

## Callers That Also Bail on Pending Switch

### `switch_away_from_terminating()` (line 1897)

```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    scheduler_lock_.unlock();
    return;   // rely on in-flight switch to dispatch the successor
}
```

This is **correct** — the terminating task is already current; the in-flight
switch will dispatch the successor once the ISR applies it. No change needed.
The comment explains: "next_task() skips current_task_ptr_ (still == &exiting
here), so it is safe to rely on the in-flight switch."

### `Scheduler::reschedule()` (line 1846)

Uses `peek_highest()` only — does NOT publish a switch. Sets
`scheduler_need_resched = true`. The actual switch is published by
`rate_monotonic_schedule()` on the next tick. No change needed.

## Why This Is Safe

1. **Superseded switch is harmless** — `next_task()` re-selects the correct
   target. One tick of lag is the worst case (the ISR had nothing to apply),
   and the next tick publishes a fresh switch.

2. **No RSP/CR3 mismatch** — the disarm zeroes all four atoms atomically with
   release ordering. The ISR epilogue checks `scheduler_save_rsp_to` — if it's
   null after disarm, it skips the switch. No half-written pair.

3. **Consistent with existing disarm pattern** — `set_current()` (lines 507–510,
   529–531), `restore_state()` (lines 2077–2080, 2086–2089), and
   `test_isolate.cpp` (lines 319–330) all use the same disarm-four-atomics
   pattern. This fix just extends it to the per-tick path.

## Edge Cases

| Scenario | Before fix | After fix |
|---|---|---|
| Pending switch from previous tick, current RUNNING | rate_monotonic_schedule bails → no scheduling → frozen window | Disarm pending → next_task() runs → correct target selected |
| Pending switch from previous tick, current BLOCKED | Override pending switch (existing code) | Same — BLOCKED case already handled |
| Pending switch from previous terminate() | Bail → terminate target runs to completion | Disarm → next_task() selects the real next task |
| Normal case (no pending switch) | No change | No change |

## Verification

Run the regression gate (same 7 classes as Phase 1+2):
1. `o1_scheduler` (20)
2. `scheduler` (51)
3. `ipc` (38)
4. `priority_inheritance` (11)
5. `lock_protocol` (34)
6. `process` (43)
7. `memory` (50)

Total: 247 tests. Record each in test-history.txt.

## Files Touched

| File | Change |
|---|---|
| `src/kernel/task/scheduler.cpp` | `rate_monotonic_schedule()` — clear all four switch atoms instead of bailing (lines 1742–1751). |
| `docs/phase3-clear-stale-switch.md` | This plan. |
| `ROADMAP.md` | Mark ReadyQueue item as completed (`[x]`). |
| `docs/scheduler-spec.md` | Update §8.3 to reflect implementation. |
