# Scheduler & Ready-Queue Specification

**Target:** Single-core x86_64, deferred context switch, O(1) bitmap-priority ready queue.

## 0. Priority Convention (binding)

**Higher numeric priority value = higher scheduling priority.**

- Priority range is 0–127 (two `uint64_t` words in the `PriorityMap` bitmap).
- `PriorityMap::get_highest_priority()` returns the **most significant set bit**
  (`find_highest_bit` → `64 + clz-based` index for 64–127), so **bit 127 = the
  highest priority** and **bit 0 = the lowest**.
- Concretely: **idle runs at priority 0** (lowest — only runs when nothing else
  is runnable); the **deadline-monitor task runs at 127** (highest); kernel
  daemons (vfsd=20, iocd=20) outrank the shell (2) and the test harness / init
  task (10 during a test cycle, 0 as background reaper).
- This has a direct consequence for **sporadic-server background priority**:
  an EXHAUSTED server's `current_priority()` returns `bg_priority_`, which MUST
  be **lower** than the task's `base_priority_` (and lower than the harness at
  10 during tests). Setting a `bg_prio` higher than the base (e.g. base=10,
  bg_prio=42) makes an exhausted task outrank the test runner and be
  preemptively dispatched mid-test — see `docs/ipc_blocking-analysis.md` §H2
  and the ss_deadline hang.

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
`move_priority()` re-buckets a task in the O(1) ready queue. Direct priority assignments (`priority = new_val`) without it leave `rq_priority_` stale. The bucket-scan in `remove()` (walks all priority queues) is a correct but O(P) stopgap, so `move_priority()` MUST be called at every priority-change site.

**Re-indexed sites (all MUST call `move_priority`):**
- `IPC::block_sender()`: `q.owner->priority = task.priority` → `move_priority` (ipc.cpp)
- `IPC::wake_sender()`: `receiver.priority = max_prio` → `move_priority` (ipc.cpp)
- `deadline_miss_handler()` (DEMOTE): `task->priority >>= 1` → `move_priority` (scheduler.cpp)
- `on_tick()` sporadic block: after `process_replenishments()` flips EXHAUSTED→ACTIVE → `move_priority` (scheduler.cpp)

**Concurrency contract (RACE-FIXED):**
- `t->priority` and `sporadic_server->state_` are plain (non-atomic, non-volatile) fields mutated concurrently by the timer ISR (sporadic `consume`/`process_replenishments`, deadline demote) and by task-context code (IPC PI under `q.lock_`). They MUST be read/written inside a critical section that excludes the timer ISR — i.e. `arch::IrqGuard` and/or `scheduler_lock_`.
- `effective_priority()` takes an internal `IrqGuard` so the composite read (`t->priority` + sporadic `state_`) is a consistent snapshot.
- The IPC PI read-modify-write + `move_priority` in `block_sender`/`wake_sender` are wrapped in `IrqGuard` (a plain `q.lock_` spinlock does NOT mask IRQs).
- `move_priority` args (old/new) computed from two `effective_priority()` calls must be produced in the same IRQ-safe section; a stale `old_prio` fed to `move_priority` desyncs the task's bucket.

**Sporadic-server priority changes (re-indexed in `on_tick`):**
- `consume()` (EXHAUSTED state): `current_priority()` returns `bg_priority_`
- `process_replenishments()` (EXHAUSTED→ACTIVE): `current_priority()` returns `base_priority_`
- Effective priority changes of the **current** task only take effect on next context switch-out (`switch_to_task` re-enqueues via `enqueue_ready()`)
- A **non-current** replenished task is re-bucketed immediately by `on_tick`'s `move_priority` call (FIX(rms-o1)); without it it sits at stale (bg) priority until the next lazy rebuild or switch-out

## 3. Ready Queue Architecture

### 3.1 `ReadyQueueManager`
- `PriorityMap` (two `uint64_t` words for priorities 0–127; **see §0 for the
  direction: higher number = higher priority**)
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
**Priority direction (see §0): higher number = higher priority.** A sporadic
server's `bg_priority_` (background, when EXHAUSTED) MUST be numerically LOWER
than its `base_priority_` (normal/active). The convention therefore also
requires `bg_priority_ < harness_priority` (10 during tests) so an exhausted
task never preempts the test runner.
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

### 4.2 When Priority Changes — Re-indexing
| Event | Effective priority change | move_priority called? | Priority corrected when? |
|---|---|---|---|
| consume() → EXHAUSTED (current task) | base → bg | No (current task not in RQ) | Next context switch-out (re-enqueue) |
| process_replenishments() → ACTIVE (non-current) | bg → base | Yes (on_tick sporadic block) | Same tick, inside scheduler_lock_+IrqGuard |
| block_sender boost | task.priority raised | Yes (IrqGuard-wrapped) | Same RMW section |
| wake_sender restore | receiver.priority restored | Yes (IrqGuard-wrapped) | Same RMW section |
| deadline DEMOTE | task.priority >>= 1 | Yes (on_tick, under lock) | Same tick |

### 4.3 `on_tick()` Budget Management (scheduler.cpp:1185–1265)
- Iterates all tasks with `sporadic_server`
- Calls `process_replenishments()` (may transition EXHAUSTED→ACTIVE)
- If current task and active: calls `consume()` (may transition ACTIVE→EXHAUSTED)
- On exhaustion: calls `reschedule()` to request deferred switch
- Does NOT re-enqueue the current task — its effective priority change is lazy (applied on switch-out)
- **Locking (RACE-FIXED):** the sporadic block, `process_deferred_kills`, `reap_orphans`, and `flush_zombies` run inside `if (lock_acquired) { arch::IrqGuard ... }`. They mutate the ready queue (`move_priority`), sporadic state, and zombie list — all fields read/written by task-context code — so they must be excluded against both the timer ISR (via IrqGuard) and a concurrent `scheduler_lock_` holder (via the `lock_acquired` gate). When the lock is contended, the tail sections are skipped for that tick (deferred) rather than raced.
- `rate_monotonic_schedule()` runs outside the gate (it does its own `try_lock`).

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
        ready_queue_.dequeue_highest();  // corrective removal
        continue;
    }
    ready_queue_.dequeue_highest();      // commit
    return candidate;
}
return idle_task_;
```

**Benefits:**
- Never orphans a dequeued task (only draining stale heads)
- Lazy-rebuild becomes dead code (remove it)
- O(1) per dispatch — no `all_tasks_` scan

### 8.2 Add `move_priority()` calls at priority-change sites

**STATUS: IMPLEMENTED (with IRQ-atomicity fix).** All priority-change sites now re-index the ready queue:
1. `IPC::block_sender()` — after `q.owner->priority = task.priority`, calls `move_priority` (wrapped in `arch::IrqGuard`)
2. `IPC::wake_sender()` — after `receiver.priority = max_prio`, calls `move_priority` (wrapped in `arch::IrqGuard`)
3. deadline DEMOTE — after `task->priority >>= 1`, calls `move_priority` (on_tick, under `scheduler_lock_`)
4. `on_tick()` sporadic block — after `process_replenishments()` (EXHAUSTED→ACTIVE) on a non-current task, calls `move_priority` (inside `if (lock_acquired) { IrqGuard }`)

**Concurrency requirement (RACE-FIXED):** `move_priority`'s `old_prio`/`new_prio` args come from two `effective_priority()` reads. If a nested IRQ (timer tick) or a concurrent `scheduler_lock_` holder mutates `t->priority`/sporadic state between the two reads, the args are stale and the task is moved into the wrong bucket. All such RMW sections therefore run inside `arch::IrqGuard` (single-core: excludes the timer ISR), and the `on_tick` sporadic path additionally gates on `scheduler_lock_`.

**Sporadic server priority transitions:**
- `consume()` EXHAUSTED: when current task, lazy-corrected on switch-out
- `process_replenishments()` ACTIVE: non-current task re-bucketed immediately by `on_tick`'s `move_priority` call

### 8.3 `rate_monotonic_schedule()` — clear stale pending switch

Replace the early-return on pending switch (line 1762) with: clear the pending switch atomics, then continue. A superseded switch is harmless — `next_task()` re-selects immediately.

This prevents the frozen-switch-window that blocks the lazy-rebuild in the current code, and remains needed until the peek_highest approach eliminates the lazy-rebuild entirely.
