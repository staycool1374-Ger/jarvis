# Scheduler Design Specification — derived from first principles

This is the **target design**. It describes what the scheduler MUST guarantee.
Where the current code satisfies an obligation, it is cited. Where it does not,
it is marked **[NOT-IMPLEMENTED]** with the gap described.

## 1. Requirements (the contract)

**R1 — Exactly one physical runner.**
**R2 — A deferred switch is applied exactly once, atomically, never to a freed stack.**
**R3 — Clean teardown.**

## 2. Derived invariants

### INV-1 — `current_task()` is RSP-authoritative
`current_task_ptr_` is a fast-path cache; physical runner is resolved by RSP ownership.
Satisfied: scheduler.cpp resolves by RSP, self-heals cache.

### INV-2 — One slot, one publisher, one applier
Single deferred-switch slot. `switch_to_task` writes it; timer ISR epilogue applies it.
Satisfied: scheduler.cpp:1509-1560 (disarm-then-publish, IrqGuard), isr_stubs.asm:106-171.

### INV-3 — ISR epilogue applies the switch, gated on nesting ≤ 2
Satisfied: isr_stubs.asm:112-113 (guard), :165 (reset), :151 (on_context_switch call).

### INV-4 — Runnable set derived from state; ready queue is a cache, not authority
`next_task()` MUST select the highest-priority runnable task.

**[NOT-IMPLEMENTED]:** The current code uses `dequeue_highest()` as the primary
source and falls back to a state-scan in the lazy-rebuild path. The documented
design intent (state-scan primary) was not implemented. See `docs/scheduler-spec.md`
§3.2 for the actual code description and §8.1 for the target fix.

**Current actual behaviour:**
- Primary: O(1) `dequeue_highest()` + while-loop filtering stale-state tasks
- Fallback: O(n) lazy-rebuild scanning `all_tasks_` (only reached when queue exhausted)
- Tasks discarded in the while-loop are orphaned (dequeued, never re-enqueued)

**Target:** `peek_highest()` + commit-dequeue (never orphan, see scheduler-spec §8.1).

### INV-5 — State-transition coherence
A task leaving the runnable states MUST be dequeued from the ready queue.

**[PARTIALLY IMPLEMENTED]:** Core BLOCKED transitions (scheduler.cpp:1943, 1971) dequeue.
`block_sender()` (ipc.cpp:375) dequeues correctly (BLOCKED then dequeue_ready).

**[NOT-IMPLEMENTED — INV-5 exception]:** `send_sync()` (ipc.cpp:274) sets state=BLOCKED
without calling `dequeue_ready()`. This is intentional per deferred-switch design (the
task continues running on CPU until the next timer tick preempts it). The WEDGE detector
treats this as "blocked-in-runq" (benign — next_task() while-loop filters it).

**Dequeue callers — verified correct:**
| Function | Dequeue | Timing |
|---|---|---|
| `terminate()` | dequeue_ready | Before state=TERMINATED |
| `remove_task()` | dequeue_ready | Before unregister |
| `unregister_task()` | dequeue_ready | Before unregister |
| `reap_orphans()` | dequeue_ready | For each reaped task |
| `cleanup_test_tasks()` | dequeue_ready | For surviving test tasks |
| `monitor_task_entry()` | dequeue_ready | Before state=BLOCKED (CORRECT ORDER) |
| `ensure_monitor()` | dequeue_ready | After state=BLOCKED (acceptable) |
| `block_sender()` | dequeue_ready | After state=BLOCKED (acceptable) |
| `send_sync()` | **NONE** | INV-5 exception |

### INV-6 — Priority changes must re-index the ready queue
`move_priority()` exists in `ReadyQueueManager` but has **zero callers**.
Direct priority assignments leave `rq_priority_` stale. The bucket-scan in `remove()`
is a correct stopgap but O(P) per call.

**[NOT-IMPLEMENTED]** — priority-inheritance re-index deferred. See scheduler-spec §2 INV-6.
Affected sites: `block_sender`, `wake_sender`, deadline DEMOTE, sporadic server transitions.

### INV-7 — Liveness under OOM
A non-yielding kernel task must still be preempted by the tick. The tick ISR does not
allocate — it only reads/writes existing scheduler structures. OOM cannot freeze the
scheduler. Satisfied: scheduler.cpp:1900-1904.

## 3. Ready-Queue Architecture

See `docs/scheduler-spec.md` §3 for the complete specification.

### Key design points
- `PriorityMap` with two uint64_t words (priorities 0–127)
- 128 per-priority `TaskQueue` arrays (already allocated)
- O(1) enqueue/dequeue via `ctz`/`clz`
- `dequeue_highest()` clears `in_ready_queue_` — task is no longer in queue
- `peek_highest()` returns head without removing
- `remove()` bucket-scans all priorities when `rq_priority_` is stale

## 4. Sporadic Server Interaction

See `docs/scheduler-spec.md` §4 for the complete specification.

### Key design points
- `current_priority()` returns `bg_priority_` when EXHAUSTED, else `base_priority_`
- Effective priority changes are **lazy** — no `move_priority()` call on EXHAUSTED↔ACTIVE
- Priority corrected on next context switch-out (re-enqueue via `enqueue_ready()`) or lazy rebuild

## 5. IPC Send/Receive

See `docs/scheduler-spec.md` §5 for the complete specification.

### Key design points
- `send_sync()` BLOCKED-without-dequeue is the sole INV-5 exception (intentional)
- `block_sender()` dequeues correctly
- Priority inheritance at `block_sender`/`wake_sender` does NOT call `move_priority()` (deferred)

## 6. Task Lifecycle

See `docs/scheduler-spec.md` §6 for the complete membership table.

### Key design points
- RUNNING tasks are NOT in the ready queue (dequeued by `dequeue_highest()`)
- `set_current()` explicitly removes old current from RQ and re-enqueues if still runnable
- `switch_to_task()` re-enqueues the preempted RUNNING task as READY (line 1717-1719)
- `switch_away_from_terminating()` re-enqueues exiting RUNNING task (line 1989)

## 7. Snapshot/Restore

See `docs/scheduler-spec.md` §7.

### Key design points
- `restore_pod()` validates head/tail; drops queues with invalid pointers
- `rebuild_ready_queue()` resets everything and re-enqueues READY tasks from `all_tasks_`
- Snapshot rebuild heals all ready-queue desyncs (stale priorities, orphaned flags, dangling pointers)
- `sporadic_server` pointer is cleared on restore (rebuilt by task fields)

## 8. WEDGE Detector

See `docs/scheduler-spec.md` §3.5.

### Key design points
- Runs every tick in `on_tick()` under `CONFIG_DEBUG`
- Two classes: blocked-in-runq (benign) and orphan (HALT)
- Orphan-halt provides deterministic evidence; critical for debugging

## 9. Target Changes

See `docs/scheduler-spec.md` §8 for the complete implementation plan:

1. **`next_task()` → peek_highest approach** — eliminate orphan while-loop, remove lazy-rebuild
2. **Add `move_priority()` at priority-change sites** — block_sender, wake_sender, DEMOTE, sporadic server transitions
3. **`rate_monotonic_schedule()` — clear stale pending switch** — prevent frozen-switch window
