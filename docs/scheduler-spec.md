# Scheduler & Ready-Queue Specification

**Target:** Single-core x86_64, deferred context switch, O(1) bitmap-priority ready queue.

## 1. Requirements (the contract)

**R1 — Exactly one physical runner.**
At every instant, exactly one task owns the live kernel stack (RSP is inside its `[kernel_stack, kernel_stack_top)`).

**R2 — A deferred switch is applied exactly once, atomically, never to a freed stack.**

**R3 — Clean teardown.** A task is freed only after (a) it is no longer the physical runner, (b) no deferred switch targets it, (c) peers waiting on its IPC are resolved.

## 2. Invariants

### INV-1 — `current_task()` is RSP-authoritative
`current_task_ptr_` is a fast-path cache; physical runner is resolved by RSP ownership.

### INV-2 — Single deferred-switch slot
Exactly one function writes the slot (`switch_to_task`); exactly one applies it (timer ISR epilogue). Last writer wins.

### INV-3 — ISR epilogue applies the switch, gated on nesting depth ≤ 2

### INV-4 — Runnable set derived from state, ready queue is a cache
`next_task()` selects by `dequeue_highest()` from the ready queue as the primary path. If the queue yields a task whose state is not READY/RUNNING, that task is dequeued (orphaned). When the queue is exhausted, a lazy-rebuild fallback walks `all_tasks_` to repopulate.

**Current implementation:**
- Primary: O(1) `dequeue_highest()` + while-loop filtering stale-state tasks
- Fallback: O(n) lazy-rebuild scanning `all_tasks_` for state==READY/RUNNING
- The fallback is the *only* self-healing for tasks that were dequeued but not dispatched

**Target design:** Replace with `peek_highest()` + commit-dequeue (see §3.1).

### INV-5 — State-transition coherence
A task leaving the runnable states (BLOCKED/WAITING/TERMINATED) MUST be dequeued from the ready queue.

**Exceptions (benign, healed by next_task() while-loop):**
- `send_sync` sets BLOCKED without dequeue — task continues on CPU until next tick preempts it (intentional per INV-4 deferred-switch window). `next_task()` filters BLOCKED-in-runq tasks in the while-loop.

### INV-6 — Priority/priority changes must re-index the ready queue
`move_priority()` exists but has **zero callers**. Direct priority assignments (`priority = new_val`) leave `rq_priority_` stale. The bucket-scan in `remove()` (walks all priority queues) is a correct but O(P) stopgap.

**Known stale-priority paths:**
- `IPC::block_sender()` (ipc.cpp:287): `q.owner->priority = task.priority`
- `IPC::wake_sender()` (ipc.cpp:~411): `receiver.priority = max_prio`
- `deadline_miss_handler()` (DEMOTE): `task->priority >>= 1`

**Sporadic-server priority changes (no re-index):**
- `consume()` (EXHAUSTED state): `current_priority()` returns `bg_priority_`
- `process_replenishments()` (EXHAUSTED→ACTIVE): `current_priority()` returns `base_priority_`
- Effective priority changes only take effect on next context switch-out (`switch_to_task` re-enqueues via `enqueue_ready()`)
- A replenished non-current task sits in the ready queue at stale (bg) priority until the next lazy rebuild or switch-out

## 3. Ready Queue Architecture

### 3.1 `ReadyQueueManager`
- `PriorityMap` (two `uint64_t` words for priorities 0–127)
- Per-priority `TaskQueue` array (`queues_[CONFIG_PRIORITY_CEILING + 1]`)
- O(1) enqueue/dequeue via bitmap `ctz`/`clz`

### 3.2 `next_task()` — Current Implementation (scheduler.cpp:479)

```
fast-path:
  while (auto *next = dequeue_highest()) {
    if (next == current || (state != READY && state != RUNNING))
      continue;  // — ORPHAN: task removed from queue, never re-enqueued
    return next;
  }
lazy-rebuild (only reached when queue is empty):
  clear_all();
  walk all_tasks_, re-enqueue READY/RUNNING tasks
  return dequeue_highest() or idle_task_
```

**Problem:** The while-loop dequeues and discards non-runnable tasks. They are never re-enqueued. The lazy-rebuild only fires when the queue is completely empty. If the queue is non-empty but every task in it has stale state, the loop spins and orphans every task.

**Also:** `rate_monotonic_schedule()` bails early when a deferred switch is already pending (line 1762), preventing the lazy-rebuild from ever running during a frozen switch window.

### 3.3 `reschedule()` — Current Implementation (scheduler.cpp:1846)
Uses `peek_highest()` (non-destructive). Only sets `scheduler_need_resched = true`. The actual switch happens on the next tick via `rate_monotonic_schedule()`.

### 3.4 `set_current()` — Current Invariant (scheduler.cpp:530)
When switching current task:
1. Remove old current from ready queue (if it was in it)
2. Re-enqueue old if it is still runnable and not current/idle
3. Set `current_task_ptr_ = &task`

This is the only place where RUNNING tasks are explicitly removed from the ready queue.

### 3.5 WEDGE Detector (scheduler.cpp:826–957, CONFIG_DEBUG)
Runs every tick in `on_tick()`. Detects two classes of corruption:
- **Blocked-in-runq:** BLOCKED/WAITING task with `in_ready_queue_=true` (benign — next_task() filters it)
- **Orphan:** READY/RUNNING task with `in_ready_queue_=true` but not physically linked in any queue → HALT

Orphan-halt provides deterministic evidence of stale flag / incomplete dequeue.

## 4. Sporadic Server Interaction

### 4.1 Priority Lifecycle
```
                ┌──────────────────┐
                │     IDLE         │  current_priority() = base_priority_
                └────────┬─────────┘
                         │ on_activation()
                         ▼
                ┌──────────────────┐
          ┌────▶│     ACTIVE       │  current_priority() = base_priority_
          │     └────────┬─────────┘
          │              │ consume() → budget == 0
          │              ▼
          │     ┌──────────────────┐
          │     │   EXHAUSTED      │  current_priority() = bg_priority_
          │     └────────┬─────────┘
          │              │ process_replenishments()
          └──────────────┘
```

### 4.2 When Priority Changes — No Re-index
| Event | Effective priority change | move_priority called? | Priority corrected when? |
|---|---|---|---|
| consume() → EXHAUSTED | base → bg | No | Next context switch-out (re-enqueue at line 1719) |
| process_replenishments() → ACTIVE | bg → base | No | Next lazy rebuild or switch-out |
| block_sender boost | task.priority raised | No | Bucket-scan remove() finds stale rq_priority_ |
| wake_sender restore | receiver.priority restored | No | Same — bucket-scan |

### 4.3 `on_tick()` Budget Management (scheduler.cpp:1049–1091)
- Iterates all tasks with `sporadic_server`
- Calls `process_replenishments()` (may transition EXHAUSTED→ACTIVE)
- If current task and active: calls `consume()` (may transition ACTIVE→EXHAUSTED)
- On exhaustion: calls `reschedule()` to request deferred switch
- Does NOT re-enqueue the current task — its effective priority change is lazy (applied on switch-out)

## 5. IPC Send/Receive Ready-Queue Interaction

### 5.1 `IPC::send()` (ipc.cpp:155)
- If dest queue full + non-block: return false
- If dest queue full + blocking: `block_sender()` → state=BLOCKED + dequeue_ready (CORRECT)
- Push message
- If dest had reply_wait or state==BLOCKED: `set_task_ready(dest)` → state=READY + enqueue_ready

### 5.2 `IPC::recv()` (ipc.cpp:241)
- Pop message, wake blocked sender via `wake_sender()` → `set_task_ready()`

### 5.3 `IPC::send_sync()` (ipc.cpp:254)
- Send message
- Set state=BLOCKED (NO dequeue_ready)
- `reschedule()` → sets need_resched flag
- Spin-wait with `hlt()` until reply arrives
- **INV-5 exception:** BLOCKED state with `in_ready_queue_=true` — intentional per deferred-switch design
- Healed by `next_task()` while-loop on next tick

### 5.4 `block_sender()` (ipc.cpp:375)
- Sets state=BLOCKED
- **Calls `dequeue_ready()`** (CORRECT — INV-5)
- Priority inheritance: `q.owner->priority = task.priority` (NO move_priority — VIOL-1)

### 5.5 `wake_sender()` (ipc.cpp:396)
- Pops oldest blocked sender
- `set_task_ready()` → state=READY + enqueue_ready
- Restores receiver priority: `receiver.priority = max_prio` (NO move_priority)

## 6. Task Lifecycle — Ready Queue Membership

| Phase | State | in_ready_queue | Physically in RQ? |
|---|---|---|---|
| create() + add_task() | READY | true (enqueued) | Yes |
| Running (dispatched) | RUNNING | false (dequeued by dequeue_highest) | No |
| Preempted (re-enqueued) | READY | true (enqueued by set_current) | Yes |
| Blocked (IPC wait) | BLOCKED | false (dequeued by dequeue_ready) | No |
| send_sync (deferred) | BLOCKED | true (NOT dequeued — INV-5 exception) | Yes |
| Terminated | TERMINATED | false (dequeued by terminate) | No |
| Zombie (post-cleanup) | TERMINATED | false (flag cleared by remove/unregister) | No |

## 7. Snapshot/Restore Ready-Queue Handling

1. `snapshot_create()` captures `ReadyQueuePOD` (heads/tails/counts/bitmap)
2. `restore_state()`:
   - `restore_pod()`: validates head/tail via `is_valid()`; drops queues with invalid pointers
   - `rebuild_ready_queue()`: resets everything, re-enqueues READY tasks from `all_tasks_` with fresh `effective_priority()`
3. `sporadic_server` pointer is cleared on restore (stale — rebuilt by task fields)

**Key consequence:** Snapshot rebuild heals all ready-queue desyncs (stale priorities, orphaned flags, dangling pointers). This is why the system stays alive despite VIOL-1.

## 8. Target Design Changes (ReadyQueue Improvement)

### 8.1 `next_task()` — peek_highest approach

Replace the destructive while-loop with a non-destructive peek:

```
while (auto *candidate = ready_queue_.peek_highest()) {
    if (candidate == current_task_ptr_ ||
        (candidate->state != READY && candidate->state != RUNNING)) {
        ready_queue_.dequeue_highest();  // remove from queue
        continue;                        // candidate was orphaned, remove it
    }
    ready_queue_.dequeue_highest();  // commit
    return candidate;
}
return idle_task_;
```

**Benefits:**
- Never orphans a dequeued task (only draining stale heads)
- Lazy-rebuild becomes dead code (remove it)
- O(1) per dispatch — no `all_tasks_` scan

### 8.2 Add `move_priority()` calls at priority-change sites

Three sites need `move_priority()`:
1. `IPC::block_sender()` — after `q.owner->priority = task.priority`: call `ready_queue_.move_priority(*q.owner, old_eff, new_eff)`
2. `IPC::wake_sender()` — after `receiver.priority = max_prio`: call `ready_queue_.move_priority(*receiver, old_eff, receiver.priority)`
3. deadline DEMOTE — after `task->priority >>= 1`: call `move_priority`

**Sporadic server priority transitions:**
- `consume()` EXHAUSTED: when current task, lazy-corrected on switch-out (line 1719)
- `process_replenishments()` ACTIVE: non-current task needs explicit re-enqueue at new priority

### 8.3 `rate_monotonic_schedule()` — clear stale pending switch

Replace the early-return on pending switch (line 1762) with: clear the pending switch atomics, then continue. A superseded switch is harmless — `next_task()` re-selects immediately.

This prevents the frozen-switch-window that blocks the lazy-rebuild in the current code, and remains needed until the peek_highest approach eliminates the lazy-rebuild entirely.
