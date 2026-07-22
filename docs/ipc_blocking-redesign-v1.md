# Scheduler / Task-Lifecycle Design — derived from first principles

This is the design to **implement against**. It is derived from the execution
model (what a single-CPU preemptive kernel *must* guarantee), not from the
current code. Where the current code already satisfies an obligation, the line
is cited as "already satisfied"; where it did not, the obligation was marked
**[MUST-FIX]** and implemented (now marked **[IMPLEMENTED]**). As of the final
status (§7) every obligation is implemented and the gate is green.

The gate (§6) judges the IMPLEMENTATION against this design, not the design
against the code.

---

## 1. Requirements (the contract)

The scheduler exists to multiplex N tasks onto one CPU. From the execution
model, exactly three requirements are non-negotiable:

**R1 — Exactly one physical runner.** At every instant, exactly one task owns
the live kernel stack (RSP is inside its `[kernel_stack, kernel_stack_top)`).
No two tasks are simultaneously "running on the CPU". Any code that asks "who is
running?" must get ONE answer, and that answer must be the RSP owner — because
the CPU will execute the RSP owner's instructions next, regardless of what any
variable claims.

**R2 — A deferred switch is applied exactly once, atomically, and never to a
freed stack.** When the scheduler decides task A should stop and task B should
run, the RSP/CR3 swap must (a) happen with no window where A is "not running"
but B is also "not running", (b) be applied exactly once (not zero, not twice),
and (c) never target a task whose stack has been freed.

**R3 — Clean teardown.** A task may be freed only after (a) it is no longer the
physical runner, and (b) no deferred switch still targets it, and (c) any peer
waiting on its IPC has been resolved (reply delivered or explicitly failed).

Everything below is a consequence of R1–R3.

---

## 2. Derived invariants

**INV-1 (from R1) — `current_task()` is RSP-authoritative.**
"Who is running" is resolved by RSP ownership, never by a cached variable.
`current_task_ptr_` is a fast-path cache; it is written only as a consequence of
a real RSP swap having already happened, or by `set_current()` for legacy
test-harness impersonation (see §5). Decision code (scheduler) and IPC routing
both call `current_task()` and therefore both see the physical runner.
Consequence: there is no logical/physical split and no impersonation window for
IPC — the task on the CPU is the task whose queue IPC touches.

*Already satisfied:* scheduler.cpp:307-339 resolves by RSP, self-heals cache.

**INV-2 (from R2) — One slot, one publisher, one applier.**
The deferred switch is a single slot: `scheduler_save_rsp_to` (where to save the
current RSP), `scheduler_load_rsp_from` (B's RSP), `scheduler_load_cr3_from`
(B's page table), `scheduler_next_task_id` (B's id). Exactly one function writes
it (`switch_to_task`); exactly one function applies it (the timer ISR epilogue).
The publisher stores a *consistent* triple: it disarms any prior pending trigger
before overwriting, and publishes the new trigger LAST, under `IrqGuard`, so no
ISR can observe a half-written pair. Last writer wins; a superseded switch is
re-selected by `next_task` on a later tick.

*Already satisfied:* scheduler.cpp:1509-1560 (disarm-then-publish, IrqGuard).
*Already satisfied:* isr_stubs.asm:106-171 applies the slot.

**INV-3 (from R2) — The applier is the timer ISR epilogue, gated on nesting.**
A deferred switch can only be applied in a context where it is safe to rewrite
RSP: the ISR epilogue, after the interrupted frame is saved. It is applied only
when `isr_nesting_depth <= 2`, because a deeper nesting means we are inside a
nested interrupt and rewriting RSP would corrupt an outer frame. After applying,
nesting is reset to 1 (the new task starts clean) and the cache is updated to
the new physical runner via `scheduler_on_context_switch`.

*Already satisfied:* isr_stubs.asm:112-113 (guard), :165 (reset), :151
(on_context_switch call). *Rationale for the `2` bound:* a normal tick is depth
1; a tick taken during a SYSCALL stub is depth 2 (the stub runs with IF set and
a pending SYSCALL frame). Depth >= 3 indicates a nested-interrupt bug and must
NOT swap. This bound is a design decision, not an observation.

**INV-4 (from R2/R1) — The timer is the only driver of publishing, and the
runnable set is derived from TASK STATE, never from a separate queue/bitmap.**
`switch_to_task` is reached only from `rate_monotonic_schedule`
(timer→on_tick→rate_monotonic_schedule) and from `on_tick`'s sporadic-
exhaustion `reschedule()` (scheduler.cpp:905). Task-context `reschedule()` does
NOT switch; it only *requests* one (sets `scheduler_need_resched`) and lets the
next tick publish+apply (INV-2). Therefore a switch is applied at most once per
tick. This removes the dual-publisher race that deadlocked ipc_blocking.

Critically, "which task is runnable" is defined by TASK STATE
(`state == READY || RUNNING`), NOT by membership in the ready queue or a bitmap
bit. `next_task()` MUST select the highest-priority runnable task by **scanning
`all_tasks_`**, excluding the current physical runner (INV-1), and ignoring the
ready queue / `PriorityMap` as the source of truth (they are a cache that can
desync). This makes INV-4 enforceable by construction: a task that is READY is
always findable, because it is found by state — there is no separate structure
that can lose it. The ready queue / bitmap becomes an optimization (and must be
kept consistent as a cache), but MUST NOT be the authority that `next_task()`
relies on.

*Violated by current code:* `next_task()` (scheduler.cpp:444-508) reads from
`ready_queue_.dequeue_highest()` / `PriorityMap` as the authority and only
falls back to a state-scan in the lazy-rebuild path (after `dequeue_highest`
returns null). The rebuild can desync (a READY task with `in_ready_queue_=false`
and cleared bitmap bit is invisible), which is the observed stall. **[IMPLEMENTED]**
— `next_task()` (scheduler.cpp:444-478) now selects by state-scan as the primary
path.

**INV-5 (from R3) — Safe teardown ordering + runnable-set coherence.**
The runnable set is defined by state (INV-4). For the ready-queue *cache* to stay
coherent (and the `[WEDGE]` detector / O(1) paths to hold), a task that leaves
the runnable states MUST be dequeued from the ready queue:
  - Any transition to BLOCKED / WAITING / TERMINATED dequeues the task
    (`dequeue_ready`) so `in_ready_queue_` is false. **[IMPLEMENTED]** — IPC
    `send_sync` (ipc.cpp:274) and the syscall recv path
    (syscall_handlers_ipc.cpp:73) set BLOCKED and dequeue; the core BLOCKED
    transitions (scheduler.cpp:1943, 1971) already dequeue. (The `block_sender`
    full-queue path deliberately does NOT dequeue — it was never in the runq as
    runnable and dequeuing there caused a ResourceTracker leak in the `ipc`
    class; the `[WEDGE]` was triggered only by `send_sync`.)
  - `terminate` must, in order:
    (a) dequeue the task from the ready queue;
    (b) if a deferred switch in the slot still targets this task, **invalidate the
        slot** so the ISR will not swap RSP into a stack about to be freed
        **[IMPLEMENTED scheduler.cpp:162-176]**;
    (c) if this task is the physical runner (INV-1: `&task == current_task()`),
        request a switch so a live successor takes over **[already satisfied,
        scheduler.cpp:188-191]**;
    (d) mark TERMINATED and wake any parent blocked in waitpid **[already
        satisfied, scheduler.cpp:150-154]**;
    (e) leave the reply queue for the peer to drain: a sender blocked in
        `send_sync` detects `dest TERMINATED` and either consumes a queued reply
        (H1, ipc.cpp:260-271) or fails cleanly. `terminate` does NOT free the
        stack; `cleanup()`/`delete` happen only after the task is no longer the
        runner and the slot no longer names it.

**INV-6 (from R1, boot/idle) — Bootstrap has no task stack.**
At boot and for the idle task, RSP is on the bootstrap stack, not a task stack.
`current_task()` must fall back to the cache in that case (scheduler.cpp:337-338),
and `init` establishes `current_task_ptr_ = idle_task_` (scheduler.cpp:214). This
is the ONLY legal non-RSP answer, and it is correct because no real task is
running yet.

---

## 3. The handshake (why R1–R3 suffice for ipc_blocking)

Sender calls `send_sync`: sends request, then blocks (state BLOCKED,
`reschedule()`). Per INV-4 the next tick publishes a switch to the receiver;
per INV-3 the ISR applies it; per INV-1 the receiver is now `current_task()` and
its `IPC::recv` reads its own queue. Receiver replies; sender's H1 re-check
(ipc.cpp:260) finds the reply (or detects death). Per INV-5 the receiver is only
freed after it is no longer the runner and its slot entry is clear. No
impersonation, no cache-lead: the design is closed by INV-1 alone.

---

## 4. Obligations on each component (implementation contract)

- **`current_task()`**: RSP scan first, cache fast-path, boot fallback. Must
  self-heal cache on scan. No decision may read `current_task_ptr_` as truth.
- **`switch_to_task`**: sole slot publisher; disarm-then-publish under IrqGuard;
  validate next's iret frame (D2 guard already at scheduler.cpp:1446-1496); must
  NOT write `current_task_ptr_` (the ISR does, per INV-3).
- **`isr_common` epilogue**: apply slot iff `depth <= 2`; reset depth to 1; call
  `scheduler_on_context_switch` to set cache = physical runner.
- **`scheduler_on_context_switch`**: resolve `scheduler_next_task_id` to TCB, set
  `current_task_ptr_`. Only writer of cache outside `current_task()`/`set_current()`.
- **`reschedule()` (task ctx)**: set `scheduler_need_resched` only; never touch
  the slot; never call `switch_to_task`.
- **`rate_monotonic_schedule`**: sole tick publisher; clears `need_resched` after
  publishing.
- **`terminate`** **[IMPLEMENTED]**: slot-invalidation — if
  `scheduler_save_rsp_to` currently targets `&task`'s stack, clear the slot
  (save_rsp_to=0, load_rsp_from=0, load_cr3_from=0, next_task_id=UINT64_MAX)
  under IrqGuard before marking TERMINATED, so the next ISR cannot swap into a
  freed stack (scheduler.cpp:162-176). Keep the existing `need_resched` request
  when `&task == current_task()`.
- **`set_current()` / `yield_as()`**: legacy cache write only — no real switch,
  no slot access, no `need_resched` change. Required so tests can impersonate
  without breaking INV-2.
- **IPC `send_sync`**: H1 re-check already correct (ipc.cpp:260-271); must remain
  the sole resolver of a dead peer's reply.

---

## 5. Out of scope (not required by R1–R3)

- `ready_queue_manager` container, `unlink_sender`, D5, move_priority, G2:
  PART J done/deferred; orthogonal to R1–R3.
- `isr_stubs.asm` `depth <= 2` guard value: a design decision (§2 INV-3), keep.
- Snapshot/restore: must preserve INV-2/INV-5 (slot clear + RSP-consistent cache
  across restore) — verify, do not change.

---

## 6. The gate (implementation judged against this design)

ONE run, full instrumentation:
```
pkill -9 -f qemu-system
timeout 40 make execute-test x86_64 debug ipc_blocking
```
CONFIG_DEBUG_IPC_SCHED on. Each step is refuted independently.

**STEP 1 (INV-2, single publisher).** Inspect source: only `switch_to_task`
writes `scheduler_save_rsp_to`. `set_current`/`reschedule`/`terminate` do not.
PASS/FAIL by grep.

**STEP 2 (INV-2/3, applier fires).** Every `[SW] cur=C next=N` has a later
`[APPLY] id=N`. FAIL if any `[SW]` unmatched ⇒ ISR dropped it (nesting stuck,
P2) or timer silent (P1).

**STEP 3 (INV-1, current = physical runner).** After `[APPLY] id=N`, later
`[SW]`/`[RS]`/`[SEND]` show `cur=N`. FAIL if `cur` pinned elsewhere.

**STEP 4 (R1–R3, handshake).** `[SEND] to=6 from=7` → receiver 6 runs →
`[WAKE] dest=7` → sender 7 `[SYNC]` → PASS. No infinite next=7/6 with no IPC.

**STEP 5 (INV-5/H1, no misroute).** No `[SYNC-FAIL] dest-gone-reply` with q>0.

**STEP 6 (all 4 PASS).** No `[STALL]`/`[WEDGE]`, no 40s timeout.

**STEP 7 (INV-5, cache sanity).** No `[APPLY]`/`cur=` names a TERMINATED id.

**STEP 8 (INV-5 [IMPLEMENTED] verification).** After test tasks TERMINATE, confirm
no subsequent `[SW]`/`[APPLY]` targets a TERMINATED id (the slot-invalidation in
`terminate` held). This is the new obligation; prior runs never checked it.

**STEP 9 (P1/P2 preconditions).** Timer ticks occur (implied by STEP 2 passing);
`isr_nesting_depth` returns to <=2 (implied by STEP 2). If STEP 2 fails, read
`isr_nesting_depth` (dump.cpp:176) and confirm ticks advance.

Gate = all steps PASS. On any FAIL: locate the violated INV, state a hypothesis
with trace evidence, change ONLY the obligated component (§4), re-run. Never
patch-and-hope.

---

## 7. Implementation status (final)

The design is **fully implemented and the gate is green**.

- INV-1 `current_task()` RSP-authoritative — already satisfied (scheduler.cpp:307).
- INV-2 single slot/publisher/applier — `switch_to_task` verified to NOT write
  `current_task_ptr_`; only `scheduler_on_context_switch` writes it. [IMPLEMENTED]
- INV-3 applier = timer ISR epilogue, depth<=2 — already satisfied.
- INV-4 runnable set derived from state — `next_task()` (scheduler.cpp:444) and
  `needs_switch()` (scheduler.cpp:438) now scan `all_tasks_` by state; the
  ready-queue bitmap is a cache only. [IMPLEMENTED]
- INV-5 teardown + runnable-set coherence — `terminate` slot-invalidation
  (scheduler.cpp:162); BLOCKED transitions dequeue (`send_sync` ipc.cpp:274,
  syscall recv syscall_handlers_ipc.cpp:73, core scheduler.cpp:1943/1971).
  [IMPLEMENTED]

Verification: `make execute-test x86_64 debug ipc_blocking` → 4/4 PASS, zero
`[STALL]`/`[WEDGE]`, stable across repeated runs (recorded in test-history.txt).
The `ipc` test class has a PRE-EXISTING runner crash (aborts ~test 15/42) that
is independent of this re-design (confirmed via `git stash` baseline) and is out
of scope for the `ipc_blocking` gate.

Outstanding (non-design, intentionally kept): the `[TICK]`/`[RMS]` diagnostic
traces in `scheduler.cpp` (gated behind `CONFIG_DEBUG_IPC_SCHED`) remain in the
tree for further analysis; they are instrumentation, not design logic, and
should be removed before commit.

---

## 8. Post-gate finding: flaky `[WEDGE]` at boot (INV-5 ready-queue coherence)

After the `ipc_blocking` gate went green, a flaky `[WEDGE] HALT` was observed at
**kernel init, before the first `memory` test body runs** (no `S: memory ...`
line, no snapshot/restore involved). It is NOT a test failure and NOT caused by
the deferred `reschedule()`.

### 8.1 Symptom
- The placeholder user-app (task 4, `user-app: placeholder (no app loaded)`)
  executes garbage at `rip=0x40046B` (ring 3) → `#UD` (SIGILL). This is
  expected/harmless (nothing real was loaded).
- The `[WEDGE]` detector then fires on the **deadline-monitor task (id 5,
  prio 127)**: `blocked-in-runq id=5 st=2(BLOCKED) inrq=1 phys=1` — a BLOCKED
  task still in the ready queue (INV-5 violation).
- A second, related manifestation: an orphaned **READY** task (task 4 / id-reuse)
  with `inrq=1`, `rn=0 rp=0` (single node), but `bitmap` bit clear and not found
  by `contains()` — i.e. `head_`/`count_`/`in_ready_queue_` desynced.

### 8.2 Root cause (analyzed, not yet fixed)
Two distinct INV-5 coherence gaps in the **boot/fault-recovery path**, unrelated
to the scheduler re-design:

1. **Monitor wake/block race** (`scheduler.cpp`): `monitor_task_entry` set
   `state = BLOCKED` *after* releasing `scheduler_lock_` (dequeue was under the
   lock, the BLOCKED store was not). The `on_tick` scan-request path
   (`s_scan_requested_` + `set_task_ready`) ran under the lock `on_tick` already
   holds, so it could observe a half-blocked monitor and re-enqueue it →
   BLOCKED+inrq. **Partial fix applied:** monitor now sets BLOCKED under the
   lock (atomic with its dequeue).

2. **Signal-terminate did not dequeue** (`kernel.cpp` `deliver_signal_to_user`):
   the fatal-signal / bad-stack / default-terminate paths set
   `task->state = TERMINATED` directly **without** calling
   `Scheduler::terminate()` (which dequeues). A terminated task left `inrq=1`
   leaves a dangling/orphaned ready-queue node; combined with TCB id-reuse and
   the intrusive `TaskQueue` links, this corrupts `head_`/`count_` for the
   priority queue → the orphaned READY+inrq+bm=0 state. **Partial fix applied:**
   those three paths now call `Scheduler::terminate()`.

3. **Underlying structural issue (NOT yet fixed):** the intrusive
   `TaskQueue` (`task_queue.cpp`) links tasks via `runq_next_/runq_prev_` inside
   the TCB. When a TCB is freed (`cleanup`/`delete`) and its id/memory is reused
   while a stale node still references it, the queue's `head_`/`count_` desync
   from `in_ready_queue_`. The `[WEDGE]` detector catches the resulting
   READY+inrq+bm=0 orphan. This is a **TCB-lifetime vs intrusive-list** bug, not
   a logic bug in `reschedule()`. Definitive fix needs either (a) clearing
   `runq_next_/runq_prev_` on every TCB free and making `TaskQueue` operations
   fully coherence-safe, or (b) replacing the intrusive queue with a
   non-intrusive structure. Left as a follow-up (see ROADMAP.md §0.3.5.x / new
   scheduler-queue item).

### 8.3 Status
- `ipc_blocking`: 4/4 PASS (stable) — deferred `reschedule()` is design-correct.
- `memory`: 45/45 on most runs; flaky `[WEDGE]` at boot remains (structural
  ready-queue coherence bug, §8.2.3). The `TaskLimitReached` leak is a separate
  flaky test-robustness issue under deferred switching.
- Applied fixes (§8.2.1, §8.2.2) are correct INV-5 improvements and stay; the
  structural queue fix (§8.2.3) is outstanding.


