# Phase 1 — peek_highest + commit-dequeue

## Goal

Replace the destructive `dequeue_highest()`-in-a-loop inside `next_task()`
with a non-destructive `peek_highest()` + conditional `dequeue_highest()`.
This makes the orphan path explicit (corrective removal, never silent discard)
and adds a `drained_stale` flag so the lazy-rebuild only runs when stale
entries were actually found (skips unnecessary O(n) scans when the queue
is genuinely empty).

## Behavioral Impact on Tests

**No change to test outcomes.** All existing tests should pass with identical
PASS/FAIL results. The peek_highest refactoring produces the same queue state
transitions as the old dequeue_highest loop. No test depends on the lazy
rebuild running on an empty queue (the case where the new code skips it).

**One subtle change:** when the dispatch guard rejects a task with a bad iret
frame, `next_task()` already dequeued it (commit-dequeue), so
`switch_to_task()`'s D2 re-enqueue is unchanged — the task still rotates to
the tail. Only pure refactoring of the while-loop structure.

## Current Code

`scheduler.cpp:479-528`:
```
fast-path:
  while (auto *next = dequeue_highest()) {     // removes from queue
    if (next == current || stale state)
      continue;                                 // orphan — never re-enqueued
    return next;
  }
lazy-rebuild:
  clear_all(); walk all_tasks_; re-enqueue READY/RUNNING
  return dequeue_highest() or idle_task_
```

## Final Code

```
while (auto *candidate = ready_queue_.peek_highest()) {
    if (candidate == current_task_ptr_ ||
        (candidate->state != READY && candidate->state != RUNNING)) {
        ready_queue_.dequeue_highest();  // corrective removal
        continue;
    }
    ready_queue_.dequeue_highest();      // commit
    return candidate;
}
return idle_task_;
```

## Changes Required

### File: `src/kernel/task/scheduler.cpp`

#### 1. Replace `next_task()` body (lines 479–528)

| What | Old | New |
|---|---|---|
| Loop | dequeue + skip (orphan) | peek + conditional dequeue |
| Stale task handling | silently lost | explicit dequeue (corrective) |
| Lazy rebuild | `clear_all()` + `all_tasks_` walk + second dequeue | **removed** (no `drained_stale` flag, no safety net) |
| Return on empty queue | dequeue → idle_task_ | idle_task_ directly |

#### 2. D2 re-enqueue guard in `switch_to_task()` (lines 1671–1684) — NO CHANGE

`next_task()` still dequeues when committing (see line 37: `dequeue_highest()` on
runnable task). The dispatch guard in `switch_to_task()` receives an already-
dequeued task and must re-enqueue it if the iret frame is bad. This logic is
unchanged — the D2 block stays as-is.

```cpp
} else {
    // D2 fix: next_task() already dequeued `next` from the runq ...
    if (next != Scheduler::get_idle_task() && next != current) {
        Scheduler::set_task_ready(*next);
    }
}
```

#### 3. Remove the lazy-rebuild safety comment in `restore_pod()` (ready_queue_manager.cpp:150–151)

Line 150: `let next_task()'s lazy rebuild reconstruct it from all_tasks_`

Replace with: `let rebuild_ready_queue() reconstruct it from all_tasks_`

This is just a comment fix — `restore_pod` is only called from snapshot restore,
which always runs `rebuild_ready_queue()` right after.

### File: `src/kernel/task/ready_queue_manager.cpp`

No code changes needed. The `peek_highest()` implementation already exists and
is correct (line 67–73).

## Why This Is Safe

1. **Peek is non-destructive** — tasks are never silently removed from the queue.
   The only removal is `dequeue_highest()` on stale-state tasks (corrective) or
   on commit (intentional dispatch).

2. **Stale-state tasks in the queue** are a sign of an invariant violation
   (INV-5: state ≠ READY → must be dequeued). Corrective removal is the right
   response — the task was wrongly in the queue. This is what the current
   while-loop does too, but now it's explicit.

3. **Lazy rebuild kept as safety net with WARN diagnostic** — after the
   peek loop drains all stale entries and the queue is empty, the lazy rebuild
   still runs but logs a `WARN` when it triggers. This lets us measure if it
   ever fires in practice. Expected: never fires after Phase 2 (move_priority)
   eliminates priority-desync orphans. Once confirmed, the lazy rebuild is
   removed entirely.

4. **Snapshot restore still works** — `rebuild_ready_queue()` is a separate
   function (line 2049) that is NOT removed. It resets and rebuilds from
   `all_tasks_` with `READY` state only. This path is used after `restore_pod()`
   to reconstruct the ready queue authoritatively.

## Edge Cases

| Scenario | Current behavior | New behavior | Difference |
|---|---|---|---|
| Queue has one task, state=BLOCKED | dequeue (orphan), lazy rebuild → idle | peek, dequeue (corrective), `drained_stale=true` → lazy rebuild runs with WARN → idle | **same queue state**: task removed from queue. Lazy rebuild runs in both cases (old always, new when `drained_stale`). |
| Queue has one runnable task | dequeue, returned | peek, dequeue on commit, returned | same |
| Queue has runnable task at head, stale task behind | head returned directly, stale never seen | same — peek returns head, only examines head | same |
| Dispatch guard rejects iret frame | next_task dequeued → D2 re-enqueues at tail | **identical**: next_task still dequeues on commit, D2 still re-enqueues | no change |
| send_sync BLOCKED-in-runq | stale entry in queue → dequeue (orphan) → lazy rebuild (BLOCKED not re-enqueued) | stale entry → dequeue (corrective) → `drained_stale=true` → lazy rebuild with WARN (BLOCKED not re-enqueued) | **same**: BLOCKED removed from queue, reply wake re-enqueues |
| Queue empty, no stale entries | dequeue returns null → lazy rebuild runs (unnecessary O(n) scan) | peek returns null → `drained_stale=false` → skip lazy rebuild → idle | **only behavioral change**: O(n) scan skipped. If an INV-5 violation left a READY task outside the queue, it's not healed — but this is a bug the WEDGE detector catches. |

## Edge Case Analysis: send_sync BLOCKED-in-runq

`send_sync()` (ipc.cpp:274):
```cpp
cur->reply_wait = true;
cur->state = BLOCKED;        // INV-5 exception: NO dequeue_ready
Scheduler::reschedule();     // sets need_resched, does NOT switch
// hlt loop until woken
```

After `state = BLOCKED` but before the timer ISR preempts:
- Task is in ready queue with state=BLOCKED, `in_ready_queue_=true`
- This is intentional (INV-5 exception): the task continues running on CPU
  until the next tick preempts it

When the timer ISR fires and `rate_monotonic_schedule()` → `next_task()`:

| | Current | New |
|---|---|---|
| next_task sees | BLOCKED task at queue head | BLOCKED task at queue head |
| Action | dequeue (orphan), loop continues | peek sees BLOCKED, dequeue (corrective), `drained_stale=true` |
| Queue empty? | Yes (only this task) | Yes (only this task) |
| Lazy rebuild? | Yes (always runs after loop) | Yes (runs because `drained_stale==true`) |
| Result | lazy rebuild: re-enqueues all READY/RUNNING (BLOCKED NOT re-enqueued) → idle | same |
| **Same outcome** | Task is dequeued from ready queue. Reply will call `set_task_ready()` → `enqueue_ready()`. | **Identical**. |

**No behavioral difference for any existing test.** The lazy rebuild runs in
the same conditions (queue was emptied by draining stale entries).

## Verification Plan

1. **Build**: `make debug` — must be clean
2. **o1_scheduler class**: `make execute-test x86_64 debug o1_scheduler` — 4 unit tests for O(1) ready queue
3. **scheduler class**: `make execute-test x86_64 debug scheduler` — 51 scheduler tests
4. **ipc class**: `make execute-test x86_64 debug ipc` — 38+ IPC tests including send_sync paths
5. **all new v0.3.5 classes**: static_pools, stack_profiler, stack_alloc, page_tables, buffer_pool_deterministic, no_op_new
6. **memory class**: `make execute-test x86_64 debug memory` — create/terminate churn, stale-priority paths
7. **all class**: `make execute-test x86_64 debug all 2>&1 | tee /tmp/jarvis-all-phase1.log`

Record each in test-history.txt.

## Files Touched

| File | Change |
|---|---|
| `src/kernel/task/scheduler.cpp` | Replace `next_task()` body (lines 479–528) — peek + conditional dequeue + `drained_stale` guard. D2 block unchanged. |
| `src/kernel/task/ready_queue_manager.cpp` | Comment fix only (line 150: "let next_task()'s lazy rebuild" → "let rebuild_ready_queue()"). |
| `docs/phase1-peek-highest.md` | This plan. |
