# Deep Analysis — H2 Residual Race & ss_deadline Hang (v0.3.9 `all` gate blockers)

**Doc ID:** NEX-DEEP-2026-08-06-001
**Branches/commits verified against:** `main`, `ae44963d` + working tree (audit-implementation state)
**Evidence runs:** `all` ×2 (227s / 234s), `ss_deadline` ×2 (with and without the audit changes — identical hang), trace ON for `all`.

The `all` gate is blocked by two independent, pre-existing defects. Both are
now root-caused with static + observed evidence, and both fixes are implemented.

---

## 1. H2 residual — stale deferred-switch arm to a terminated task

### 1.1 Symptom (observed, `all` run 1)
```
[SW] cur=7 next=1 ...         # arm 7→1 (harness)
[SW] cur=7 next=1 ...         # re-arm
[APPLY] id=1 cur=1            # switch to harness applied
[SW] cur=1 next=6 ...         # arm 1→6 (task 6)
[APPLY] id=6 cur=1            # APPLIED to task 6, but current-cache stayed 1
[H2W] orphan-displaced tick=0 cur_rsp=0xFFFF800000A57B80
     ctx_rsp=0xFFFF900000032920 kst=0xFFFF900000023000-0xFFFF900000033000
```
`[APPLY] id=6 cur=1` is the smoking gun: `scheduler_on_context_switch`
(scheduler.cpp:2835) loaded `scheduler_next_task_id == 6`, called
`find_task(6)` → **null** (task 6 already removed from `id_table_`), so
`set_current_task(6)` no-oped and the cache stayed at 1 — but the ISR had
**already iretq'd** onto task 6's saved `context.rsp`. The CPU then executed on
a freed/foreign page (`0xFFFF800000A57B80`), the harness was displaced, and the
suite froze.

### 1.2 Root cause
The deferred-switch pair (`scheduler_save_rsp_to` / `scheduler_load_rsp_from` /
`_load_cr3_from` / `_next_task_id` / `_load_kstack_base/top`) is published by
`switch_to_task()` (scheduler.cpp:1973-2036) and normally consumed by the same
timer ISR's epilogue. When the epilogue **skips** the apply (nested-ISR depth
guard `ja .restore` at isr_stubs.asm:130-131, or the generation re-check
`jne .restore` at :165-166), the arm survives into a later ISR. In that
window the CPU is back in task context (IF=1), and the harness can **terminate
the armed target**.

`Scheduler::terminate()` (scheduler.cpp:343) → `release_zombie()` (:152) removes
the task from `all_tasks_`, `deadline_list_`, `id_table_` — **but never touches
the switch atoms**. By contrast `remove_task()` (:544-546) and `unregister_task()`
(:569-571) DO clear them. So a pending arm to a terminated task is left live;
the next ISR epilogue applies it (the apply-side RSP-owner check passes because
the freed stack's VA range still satisfies `[kstack_base, kstack_top)`), iretq's
onto freed/foreign memory, and `find_task(id)` returns null at
`scheduler_on_context_switch` — current-cache divergence + harness displacement.

This is the residual H2 mechanism behind ROADMAP §v0.3.9 / `docs/specs/ipc.md §4`.
Layers 4-6 (dispatch-guard frame.rsp check, scratch-save, apply-side RSP-owner
check) all validate the *RSP*, not the *liveness of the target task*. The task
can be freed between arm and apply with none of them firing.

### 1.3 Fix (implemented)
`invalidate_pending_switch_to(task_id)` — a static helper in scheduler.cpp that,
when the pending `scheduler_next_task_id` equals the removed task's id, clears
all switch atoms **and bumps `scheduler_switch_generation`**. The generation
bump makes any ISR that captured the pre-clear generation (isr_stubs.asm:136)
fail its re-check (:165-166) and skip the apply; the atom clear makes any ISR
entering after the invalidation see `save_rsp_to == 0` and skip.

Called from:
- `Scheduler::release_zombie()` (the terminate/self-terminate path),
- `Scheduler::reap_orphans()` free loop (direct TERMINATED reaping).

Deliberately **not** called for the self-terminating *current* task: its pending
arm (published at terminate():378) targets a valid successor, and `release_zombie`
runs *before* that arm is published, so `scheduler_next_task_id != task.id` and
the check is a no-op — the switch-away is preserved.

### 1.4 Expected effect
A stale arm to a removed task is neutralized before any ISR epilogue can apply
it; `find_task(id)==null` at apply time becomes unreachable for the
terminate/reap paths, eliminating the `[APPLY] id=X cur≠X` divergence and the
harness displacement on freed stacks.

---

## 2. ss_deadline — EXHAUSTED SS task starved below the harness

### 2.1 Symptom (observed, `ss_deadline` standalone + `all` run 2 test 469)
The suite never prints `S: ss_deadline ... 1/2`. The trace shows:
```
[RS] cur=1 next=6 hi=11          # harness(1) → helper(6, prio 11) via reschedule
[SW] cur=1 next=6 ... ; [APPLY] id=6
[RMS] cur=6 next=1 ... ; [SW]/[APPLY] id=1     # helper pulled back to harness
[TICK] t=93..78175 lk=1 ... nt=6               # harness spins forever
```
After the helper exhausts its SS budget, its effective priority collapses to
`bg_priority_` (2), and the `harness_nonpreempt` guard in
`rate_monotonic_schedule()` (scheduler.cpp:2072-2082) permanently refuses to
preempt the harness for it:
```
harness_nonpreempt && !scheduler_need_resched  →  if (highest_ready < cur_prio) return;
                                                    # 2 < 10 → RETURN, no switch
```
The helper never runs again, so it never reaches `gate.wait()` → never BLOCKED
→ the harness's `while (helper->state != BLOCKED)` spins forever.

### 2.2 Root cause
`test_ss_deadline.cpp` helper lambda (`spawn_ss_exhausted`, :48-73) consumes its
budget **first**:
```
on_activation();  consume×5 → EXHAUSTED (eff prio = bg_prio 2)
while (ticks() <= deadline_ticks) pause();   // ← needs ~10 ticks of CPU
gate.wait();                                  // ← never reached
```
Once `consume()` exhausts the 3-tick budget, `SporadicServer::current_priority()`
(sporadic_server.hpp:113-115) returns `bg_priority_` = 2 < harness 10. During the
subsequent ~10-tick busy-wait the helper is below the harness, so the
`harness_nonpreempt` RMS guard starves it. `effective_priority()`
(scheduler.cpp:98-116) returns the SS's background priority for an EXHAUSTED
server, so both the ready-queue position and the RMS guard see prio 2.
(Additionally, if the SS is ACTIVE during the busy-wait, `on_tick` auto-consumes
the budget — `if (t == cur && is_active()) consume()` at scheduler.cpp:1309 —
so an ACTIVE server also exhausts mid-wait; the fix keeps the server IDLE until
after the busy-wait.)

This matches ROADMAP_done.md v0.3.9 issue (4): "an EXHAUSTED SS task at bg_prio
2 cannot be re-dispatched after gate.post() (the harness's TERMINATED wait
spins)". The kernel's demotion behavior is correct SS semantics; the *test*
demands an exhausted task keep running, which is impossible below the harness.

### 2.3 Fix (implemented, test-side)
- **Reorder the helper lambda**: busy-wait past the real deadline at **nominal**
  priority (prio 11 > harness 10 — auto re-dispatched every tick via the RMS
  guard, since `highest_ready(11) < cur_prio(10)` is false), **then** activate +
  exhaust (`consume×5` → EXHAUSTED), **then** `gate.wait()`. The deadline passes
  while the task is live; the scan captures EXHAUSTED context.
- Keep the SS **IDLE** during the busy-wait (no `on_activation` until after) so
  `on_tick`'s auto-consume cannot demote the task mid-wait.
- Change the harness's `while (state != BLOCKED)` and `while (state != TERMINATED)`
  spins from bare `pause()` to `Scheduler::reschedule()` — the only way to
  dispatch the now-exhausted (eff prio 2) helper for `gate.wait()` and for
  self-termination after `gate.post()`.

### 2.4 Expected effect
`ss_deadline` 2/2 completes: helper genuinely exhausts, real deadline passes,
scan fires with EXHAUSTED context (budget 0), post → self-terminate → clean
teardown via `remove_task`.

---

## 3. Fix discipline notes
- Both fixes are additive and confined to their defect paths; neither alters the
  canonical ISR switch path (disassembly-verified for the abort-path change in
  `ae44963d`).
- Validation plan: `make build` (check-style gate); class gates `ss_deadline`,
  `deadline_miss`, `deadline_action`, `deadline_recovery`, `wcet_overrun`,
  `sporadic`, `timing`, `ipc`, `scheduler`, `atomic`; then the `all` gate with
  `CONFIG_DEBUG_IPC_SCHED` ON per the debug-gate procedure. `test-history.txt`
  rows appended after every run.

---

## 4. Additional pre-existing blockers unmasked (2026-08-06)

Fixing the two audit targets let the `all` gate reach later classes, exposing
two more pre-existing failures (both reproduced at baseline):

### 4.1 `wcet` — semaphore waiter-array overflow + INV-4 self-termination
`test_wcet_scheduler.cpp` created **40** tasks all blocking on one gate, but
`CONFIG_SYNC_MAX_WAITERS = 32` → the 33rd `wait()` failed `add_waiter()` →
`ENSURE(added)` panic (semaphore.cpp:185).  Additionally the helper lambda
returned immediately after `wait()` (INV-4), self-terminating before the harness
observed BLOCKED → hang once the overflow was removed.  **Fix:** population
40→30 (< 32) and the BLOCKED-spin pattern.  `wcet` 1/1 PASS (was panic).

### 4.2 `priority_inheritance` — mutex PCP spin vs. genuine blocking (T2-3)
The holder now correctly stays BLOCKED (BLOCKED-spin added), so the test
proceeds to `spawn_contender` (prio 20).  The contender calls `Mutex::lock()`
on the mutex held by the prio-11 holder.  `Mutex::lock()` (mutex.cpp:227-253)
only BLOCKS when the PCP ceiling path is active (`priority_ceiling_ > 0 &&
task->system_ceiling_ > 0 && task->priority <= system_ceiling_`); the test's
`Mutex::init()` uses ceiling 0, so the lock spins in the retry loop and panics
`Mutex::lock() exhausted PCP retry budget`.  The tests were written assuming
genuine blocking (`waiting_on_mutex`), a documented spec contradiction
(audits/test-suite-v0.3.10.md T2-3, ROADMAP_done v0.3.9 issue (2)): the test
suite framework matched the panic as an "expected panic" and the class reported
PASS spuriously (no `S:` line).  **Not fixed in this pass** — it is a separate
kernel/test model mismatch beyond the two audit targets.  Needs either a mutex
ceiling in the tests or a decision on whether `Mutex::lock()` should genuinely
block for ceiling-0 mutexes.

### 4.3 H2 residual status
The apply-side liveness + ownership re-check (section 1.3) plus the harness
`context.rsp` live-save (save-target always `&TASK_STACK_PTR(current)`) and the
dispatch-guard harness-boot exemption reduced the ipc-class H2W flake from
~1-in-3 to clean across 8 consecutive runs; the `all` gate now passes tests
1–476 (including the ipc cluster at 77/78, ss_deadline 469/470, wcet 476)
before reaching the priority_inheritance blocker at 477.

### 4.4 Hardware-watchpoint session (2026-08-06) — H2 residual live capture
Attempted to pin the residual with hardware watchpoints per ROADMAP §v0.3.9.

**QEMU gdb-stub hardware watchpoints are confirmed BROKEN** (as documented):
- lldb `WatchpointCreateByAddress` on `scheduler_next_task_id` (0xFFFF8000002E52B0)
  creates successfully but never fires during a full boot + ipc class run.
- x86_64-elf-gdb `watch *(uint64*)0x...` likewise sets "Hardware watchpoint 1"
  but never triggers.
So the documented "working instrument" is the kernel recorder + caller tracing,
not the QEMU stub.

**Live capture (kernel-side caller tags on every [SW] arm; ipc class, run 10):**
```
[SW] cur=1 next=6 rsp=0xFFFF9000...24224 caller=0xFFFF800000294395   # arm A
[SW] cur=1 next=0 rsp=0xFFFF8000...310160 caller=0xFFFF800000294395   # arm B
[APPLY] id=0 cur=0                                                    # idle → harness stranded
```
- **Two deferred-switch arms published back-to-back from the SAME call site**
  (identical return address), with NO [APPLY] or [TICK] between them.
- The second arm selects **idle** — `next_task()` returns idle because the first
  arm's `next_task()` already DEQUEUED task 6 (which is never re-dispatched).
- Applying the idle arm iretq's the harness into `idle_task_main` → the
  `[DIAG] idle loop count=186A0` hang.
- An `[ARM-SUPER]` probe (publish while `save_rsp_to != 0`) does **not** fire:
  the atoms are already cleared between the two arms (RMS clears pending arms
  every tick — `[RMS-CLR] pending=...`), so it is not a simple pending-supersede.
- `reap_orphans()` is confirmed NOT running during tests (entry probe REAP=0
  across 5 runs) — the addr2line hit on `reap_orphans()` for the arm caller was
  a stack/inlining artifact.
- Every added diagnostic (caller field, ARM-SUPER, RMS-CLR, REAP) perturbs the
  race away (repro rate drops to ~0/5), consistent with the documented
  "a single per-tick instruction perturbs it" (ROADMAP_done).

**Mechanism (refined):** a runnable task (task 6) is dequeued by
`next_task()` during an arm publish, the arm is then skipped/cleared before its
ISR epilogue applies it (generation change / RMS clear / set_current), and the
next `next_task()` falls through to idle — the dequeued task is stranded
(INV-2) and the harness is iretq'd into the idle loop.  The definitive fix
requires re-enqueueing the dequeued target when an arm is dropped, or a
hardware-watchpoint session on a host that supports it (QEMU's stub does not).
Tracked in ROADMAP §v0.3.9.

### 4.5 Global H2 event ring (implemented) — root cause pinned
Extended the per-TCB `debug_switch_ring[4]` idiom into a GLOBAL in-memory
event ring (kernel::debug::g_h2_ring[512], 6×uint64 per record), recording
every deferred-switch event with the atoms + generation + ISR depth:
`ARM`, `APPLY`, `SKIP` (asm gen-skip → scheduler_record_skip), `CLR-RMS`,
`CLR-SET`, `CLR-MISC`, `IDLE-ARM`, `REENQ`.  No serial I/O in the hot path
(the perturbation that made the race vanish); dump post-hang via
`h2_dump_ring()` or `x/120gx &kernel::debug::g_h2_ring`.

**The ring CAUGHT the root cause.**  Live capture at `ipc` test 21:
```
[ARM]     a=0x6 b=0xFFFF800000A4FF40   # arm harness→task6; its context.rsp is a
                                        #   direct-map address (displaced)
[CLR-MISC]a=0x6                          # apply-side validation aborted the arm
[ARM]     a=0x0                          # next arm selects idle — task6 stranded
[IDLE-ARM]a=0x1                          #   (next_task() skipped the harness)
[APPLY]   a=0x0                          # harness iretq'd into the idle loop → hang
```
The validation abort (drop of a stale arm) left the preempted harness READY +
still in the runq (INV-4) — so `next_task()` skipped it (a RUNNING-current
task) and fell through to idle.  The `t==null` path (target already removed)
did the same.

**Fixes (from the ring evidence):**
1. `scheduler_validate_pending_switch` drop_arm now restores the CURRENT task
   to RUNNING + dequeues it from the runq (undoing switch_to_task's
   READY+enqueue side effects) on EVERY abort path, and
2. re-enqueues the dequeued target via `set_task_ready` when it is still alive.

Effect: ipc flake drops from ~1-in-3 to ~1-in-14 hangs (direct-QEMU runs);
the idle-apply sequence is eliminated (the harness is RUNNING, so the
`!(next==idle && current RUNNING)` RMS guard protects it).  A rarer residual
remains (~7% direct / ~30% under the UART+expect harness) where the harness
hlt-waits with no further arms — a post-abort task-lifecycle state still under
investigation.  ROADMAP §v0.3.9 stays open.
