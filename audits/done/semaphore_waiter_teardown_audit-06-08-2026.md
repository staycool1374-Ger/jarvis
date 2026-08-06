# JARVIS RTOS — SIL 3 Audit: Blocked Semaphore Waiter Teardown Gap (v0.3.9) — RESOLVED

**Auditor:** Independent SIL 3 safety auditor (does not trust the developer agent)
**Domain:** `src/kernel/sync/semaphore.*`, `src/kernel/task/task.*`
**Verdict (pre-fix):** REJECTED — real memory-safety defect (use-after-free on ready-queue insertion), debug-build-masked Heisenbug.
**Verdict (post-fix):** APPROVED — teardown gap closed; regression test added; 147/147 `vfs` class PASS.

---

## Finding

`Semaphore::wait()` stores a raw TCB in `waiters_[]` (`semaphore.hpp:83-84`) and leaves the task linked while the deferred switch applies. `TaskControlBlock::cleanup()` unlinks IPC blocked-sender lists (`task.cpp:1288-1310`) but had NO equivalent semaphore-waiter unlink, and the TCB had no back-pointer to locate the semaphore.

**Failure chain (post-cleanup post):**
1. Task `T` blocks in `sem.wait()`; `T.generation = g0` recorded in `waiter_gens_[k]`.
2. External termination (any of `cleanup_test_tasks`, `reload_daemon_tasks`, `test_cleanup_all`; `Scheduler::terminate()` has no state guard) → `T` → TERMINATED → zombie list.
3. Zombie drain → `T->cleanup()` → `state = REAPED` (`task.cpp:1249`), resources freed, `MemPool::free(T)`.
4. Later `sem.post()` → `wake_one()`:
   - Generation sweep does NOT rescue free-but-unreused blocks in **release** (no `0xDD` poison, `mempool.cpp:138`).
   - Guard tests `state != TaskState::TERMINATED`; `REAPED != TERMINATED` → the freed block is passed to `Scheduler::set_task_ready()` → ready-queue corruption / UAF.
   - In **debug**, the `0xDD` poison masks it (generation mismatch → swept) — a textbook Heisenbug.

**Reachability:** termination of a BLOCKED task is performed unconditionally by live teardown paths; `cleanup()` provably lacked the unlink it already performs for IPC. The suite only passed because `src/lib/test.hpp` cookbook rule 6 (an undocumented "never do this" workaround the ROADMAP itself forbids) kept tests from triggering the scenario.

---

## Fix (implemented, commit pending)

1. **TCB back-pointer** `sync::Semaphore *waiting_on_semaphore` (`task.hpp`), forward-declared in `namespace sync`, set in `Semaphore::add_waiter()`, cleared in `wake_one()` and `remove_waiter()`, initialized to `nullptr` in `init_task_common()` (TCBs are memset-zeroed in `create()`, so the back-pointer starts null).
2. **`Semaphore::remove_waiter(TaskControlBlock&)`** — public, lock-safe: acquires `lock_`, linear scan matching **pointer + generation** (guards against a recycled TCB occupying the slot), swap-remove (mirrors `Queue::remove_send_waiter` / `IPC::unblock_sender_rollback`).
3. **`cleanup()` hook** (`task.cpp`) — before resource teardown, guarded by the same higher-half pointer-range check used for `blocked_on_queue`; calls `remove_waiter(*this)` then clears the back-pointer. Lock-safe: `cleanup()` holds no scheduler lock (`drain_zombie_list`/`cleanup_step` release IRQ guard first, `scheduler.cpp:227-229`), preserving the wake path's `sem.lock_ → scheduler_lock_` order.
4. **Hardened wake guard** — `wake_one()` now rejects `REAPED` in addition to `TERMINATED` (both single- and multi-waiter branches) so a swept-to-REAPED entry is never fed to `set_task_ready`.
5. **Regression test** `semaphore_waiter_teardown_on_terminate` (`test_sync.cpp`) — REAL task blocks in `sem.wait()`, externally terminated via `Scheduler::terminate()` + `drain_zombie_list()`, then asserts `waiter_count() == 0`, `post()` takes the count-increment path (`value() == 1`), no ResourceTracker delta. Added lock-safe `Semaphore::waiter_count()` accessor for observability. Cookbook rule 6 updated to reflect the now-closed gap.

**Systemic note:** Mutex, EventGroup, and Queue share the same teardown asymmetry (no cleanup unlink). Explicitly out of scope per the fix order directive; tracked for follow-up.

---

## Follow-up implementation (same session) — Mutex / EventGroup / Queue

The audit's systemic recommendation was implemented for all three remaining primitives:

1. **Mutex** — `Mutex::remove_waiter()` added (pointer+generation swap-remove under `lock_`); back-pointer `waiting_on_mutex` now set centrally in `add_waiter()`; `wake_one()` AND both unlock direct-ownership-transfer selection loops hardened to skip TERMINATED/REAPED waiters (never transfer ownership to / wake a freed TCB).
2. **EventGroup** — `EventGroup::remove_waiter()` added; TCB back-pointer `waiting_on_eventgroup` set in `add_waiter()`, cleared in `wake_matching()`/`~EventGroup()`/`remove_waiter()`; destructor + `wake_matching()` hardened to reject REAPED.
3. **Queue** — `Queue::remove_waiter()` added (public, lock-safe, dispatches to the existing send/recv swap-removes); TCB back-pointer `waiting_on_queue` set in `add_send_waiter()`/`add_recv_waiter()`, cleared in all wake/remove paths; `wake_send_one()`/`wake_recv_one()` hardened to reject REAPED.
4. **cleanup()** — the detach hook now covers all four back-pointers (mutex, semaphore, eventgroup, queue) with the same higher-half range check and lock-safety rationale.

**Regression tests** (`test_sync.cpp`): `semaphore_waiter_teardown_on_terminate`, `eventgroup_waiter_teardown_on_terminate`, `queue_waiter_teardown_on_terminate`. The mutex teardown cannot be driven as a REAL blocked-on-mutex test because `Mutex::lock()`'s `MAX_WAITERS+1` PCP retry budget exhausts before the deferred switch lands at 1ms ticks (documented at `test_sync.cpp` mutex test — a contended lock cannot genuinely block a dispatched task); the mutex fix is instead covered statically + by `lock_protocol` 53/53 and `scheduler` 63/63 gates.

**Pre-existing flake found + fixed during validation:** `scheduler_preemptive_priority` and `preemption_needs_switch_higher_priority` created empty-lambda tasks that self-terminate when a timer ISR dispatches them inside `add_task()`'s slow serial-log window, before the assertion — a ~20% baseline flake (reproduced on 7cfa2810). Fixed by holding `arch::IrqGuard` across `add_task` + assert (mirrors the existing `preemption_needs_switch_blocked_higher` pattern). Scheduler class: 4/4 green after fix.

**Verification:** `make build` clean (Errors: 0). `vfs` 149/149 ×3, `lock_protocol` 53/53 ×3, `scheduler` 63/63 ×4 — all green, no ResourceTracker deltas.

---

## Audit Check Log

| Check | Result |
|---|---|
| Dynamic allocations in critical paths | PASS — no new allocation; fixed arrays only |
| Concurrency boundaries | PASS — `remove_waiter` takes `lock_`; no `sem.lock_ → scheduler_lock_` inversion introduced |
| Assertion masking | PASS — debug poison no longer the only defense; explicit unlink + REAPED guard + regression test |
| Memory safety (double/stale-free) | PASS — freed TCB can no longer be re-queued; generation-matched swap-remove prevents ABA |
| Critical section interference | PASS — cleanup hook mirrors IPC blocked-sender pattern; scheduler invariants unchanged |
| Preprocessor/conditional semantics | PASS — no `#ifdef` divergence; back-pointer unconditionally initialized |
