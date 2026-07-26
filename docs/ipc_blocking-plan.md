# Ready-Queue Fix Plan — peek_highest + move_priority

Supersedes the earlier plan (which proposed clearing stale triggers + re-enqueue
dropped tasks). The peek_highest approach eliminates the root cause more cleanly.

## Phase 1 — Fix `next_task()`: peek_highest + commit-dequeue

**Goal:** Never orphan a dequeued task. Make the lazy-rebuild dead code.

### Code change — scheduler.cpp `next_task()` (line 479)

Replace:
```cpp
{
    auto *next = ready_queue_.dequeue_highest();
    while (next && (next == current_task_ptr_ ||
           (next->state != READY && next->state != RUNNING))) {
        next = ready_queue_.dequeue_higthest();  // ORPHAN
    }
    if (next) return next;
}
// Lazy rebuild
ready_queue_.clear_all();
walk all_tasks_...
```

With:
```cpp
{
    while (auto *candidate = ready_queue_.peek_highest()) {
        if (candidate == current_task_ptr_ ||
            (candidate->state != READY && candidate->state != RUNNING)) {
            ready_queue_.dequeue_highest();  // drain stale head
            continue;
        }
        ready_queue_.dequeue_highest();  // commit
        return candidate;
    }
}
// No lazy rebuild needed
```

**Remove** the lazy-rebuild block (lines 496-528).

### Why this works
- `peek_highest()` is non-destructive — tasks are never silently removed
- A task with stale state is explicitly `dequeue_highest()`'d (removed from queue) — this is a correction, not an orphan
- Once the queue is empty of runnable tasks, fall through to `idle_task_`
- The lazy-rebuild was the only way orphaned tasks were recovered; with no orphans, it is dead code

### Keep `rebuild_ready_queue()` (line 2049)
This is a separate function used only by snapshot restore. It has different semantics:
resets everything, only enqueues READY tasks (not RUNNING), clears links on non-ready tasks.

## Phase 2 — Add `move_priority()` at priority-inheritance sites

**Goal:** Keep `rq_priority_` authoritative; eliminate bucket-scan in `remove()`.

### Site 1 — `IPC::block_sender()` (ipc.cpp:287)

Before:
```cpp
q.owner->priority = task.priority;
```

After:
```cpp
uint64_t old_prio = effective_priority(q.owner);
q.owner->priority = task.priority;
uint64_t new_prio = effective_priority(q.owner);
if (old_prio != new_prio)
    ready_queue_.move_priority(*q.owner, old_prio, new_prio);
```

### Site 2 — `IPC::wake_sender()` (ipc.cpp:~411)

Before:
```cpp
receiver.priority = max_prio;
```

After:
```cpp
uint64_t old_prio = effective_priority(&receiver);
receiver.priority = max_prio;
uint64_t new_prio = effective_priority(&receiver);
if (old_prio != new_prio)
    ready_queue_.move_priority(receiver, old_prio, new_prio);
```

### Site 3 — Deadline DEMOTE action (scheduler.cpp `deadline_miss_handler`)

Before:
```cpp
task->priority >>= 1;
```

After:
```cpp
uint64_t old_prio = effective_priority(task);
task->priority >>= 1;
uint64_t new_prio = effective_priority(task);
if (old_prio != new_prio)
    ready_queue_.move_priority(*task, old_prio, new_prio);
```

### Site 4 — Sporadic server transitions

When `process_replenishments()` transitions EXHAUSTED→ACTIVE for a non-current task:
- After state change, call `move_priority()` if the task is in the ready queue
- Track: only needed if `task->in_ready_queue_` is true

When `consume()` transitions ACTIVE→EXHAUSTED for the current task:
- No immediate action needed — lazy-corrected on switch-out (line 1717-1719 re-enqueue)
- But only if the current task is preempted in the same tick; if it's the only task, it stays at stale priority until next preemption

## Phase 3 — rate_monotonic_schedule: clear stale pending switch

**Goal:** Prevent frozen-switch window from blocking the scheduler.

### Code change — scheduler.cpp `rate_monotonic_schedule()` (line 1762)

Replace:
```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    auto *cur = current_task();
    if (!cur || cur->state == TaskState::RUNNING ||
        cur->state == TaskState::READY) {
        scheduler_lock_.unlock();
        return;  // BAIL — frozen switch window
    }
    ...
}
```

With: always clear the pending switch before proceeding:
```cpp
if (__atomic_load_n(&scheduler_save_rsp_to, __ATOMIC_ACQUIRE) != 0) {
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_next_task_id, (uint64_t)-1, __ATOMIC_RELEASE);
    // Continue to next_task() — superseded switch is harmless
}
```

## Verification

### Per-class runs (in order):
1. `o1_scheduler` — O(1) ready-queue unit tests
2. `scheduler` — scheduler integration tests
3. `ipc` — IPC + send_sync + block_sender paths
4. `memory` — create/terminate churn, stale-priority paths
5. `static_pools` — verify no regression in pool tests
6. `stack_profiler` + `stack_alloc` + `page_tables` + `buffer_pool_deterministic` + `no_op_new`
7. `all` — full regression suite

### Expected regressions to monitor:
- **move_priority interacts with snapshot restore** — restore clears `sporadic_server` pointer and rebuilds RQ, so priority changes before snapshot are healed. But if a priority change happens during a test, and the test snapshots, the new priority is in the TCB fields and `move_priority` moved the task to the correct priority queue — so the restored POD should be consistent. No regression expected.
- **send_sync BLOCKED-without-dequeue** — the peek_highest approach still filters non-READY tasks in the while-loop, so blocked-in-runq is handled identically to the current code.
- **rate_monotonic_schedule clears pending switch** — a published switch is dropped, but `next_task()` re-selects immediately. Risk: the ISR epilogue has nothing to apply for one tick, causing a one-tick lag. Acceptable — the next tick publishes a fresh switch.
