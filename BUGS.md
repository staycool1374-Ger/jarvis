# Open Issues

## Kernel — Memory

### ID: #013 — MempoolFragmentation test hangs at test 438
- **Description:** `MempoolFragmentation` in `test_resource_exhaustion.cpp` hangs during `make test-all-debug` at test index 438. The test allocates 20 objects per MemPool size class (9 classes: 16–4096 bytes), fills them with 0xA5, then frees in reverse order. On some runs the loop over size 4096 (largest class) deadlocks or livelocks — likely a MemPool internal corruption or infinite loop in `MemPool::free()` when returning a large block to a fragmented pool.
- **Root Cause:** Not yet determined. Suspected MemPool bitmap/free-list corruption when freeing the last block of a particular size class in reverse order. Pre-existing — confirmed in baseline.
- **Fix:** Temporarily disabled by commenting out `REGISTER_CLASS(MempoolFragmentation)` in `test_resource_exhaustion.cpp`.
- **Severity:** Medium (blocks full test suite for release verification)
- **Domain:** Kernel — Memory / Test Infrastructure
- **Status:** Open (disabled)

### ID: #021 — Flaky scheduler deadlock/HANG in `memory` class at ~test 22 (no #PF, no panic)
 - **Description:** After the #020 #PF fixes (commit 2026-07-20), the `memory` class passes **45/45** on a clean run (~37 s) but **intermittently HANGS** (TIMEOUT, no TEST SUMMARY) at ~test 22 — i.e. right after `buffer_pool_exhaustion` (test 21/45). The `user-app` placeholder (ID=4) has already terminated by that point, and the hang occurs with **no #PF and no kernel panic** in the log, so this is a **scheduling deadlock / lost-wakeup**, not a memory fault. Originally observed 1/3 runs hanging.
 - **Investigation (Jul 2026):**
   - Added `harness_task_ptr_` infrastructure: `set_harness_task()` in `scheduler.hpp`, task names, `init` named in `kernel.cpp`, and a `harness_nonpreempt` guard in `rate_monotonic_schedule()` that prevents the harness from being preempted during test-active. This reduced hang frequency from ~30-50% to ~10-30% but did **not** eliminate it.
   - A **built-in wedge detector** (`[WEDGE] orphan=…`) already exists at `scheduler.cpp:761-790` — it iterates all tasks and fires when a task has `state==READY/RUNNING` + `in_ready_queue_=true` but is not physically linked in any priority queue. **No `[WEDGE]` ever appeared in the serial log during a hang.**
   - An `[ORPHAN-NEXT]` diagnostic was added at `next_task()` return-to-idle path (fires if harness is `READY && !in_ready_queue_` when idle is selected). **Did not fire during any hang.**
   - **Conclusion:** The harness is **not** in the orphan state (`READY + in_rq=true + not-in-queue`). It must be **BLOCKED** with a **lost wakeup** — the test task finishes but the wakeup for the harness is never delivered.
   - Wedge signature from serial log after a hang: `[SW] cur=1 next=4` → `[APPLY] id=4` → `user-app` placeholder prints → `_exit(0)` → `switch_away_from_terminating` → `next_task()` → idle → hang. Harness (task 1) never appears as `next`.
 - **Root Cause:** Unconfirmed. Strongly correlated with the large **uncommitted scheduler rework** (`scheduler.cpp` +~452 lines, `ready_queue_manager.cpp`, `task_queue.cpp`, `scheduler.hpp`, `kernel.cpp`). The hang is timing-dependent, consistent with a harness-wakeup path that is lost when a specific tick/preemption ordering occurs between the terminating test task and the harness's block/wake cycle.
   - Candidate mechanisms: (a) `restore_pod` restores stale queue nodes and `rebuild_ready_queue` doesn't properly reconcile task states; (b) the `wake_waiting_parent` path in the rework skips a wakeup under a racing condition; (c) `set_current_task`/`enqueue()` double-enqueue guard silently refuses a task whose `in_ready_queue_` is stale-`true` from `restore_pod`, causing a wakeup to be lost silently.
   - **Eliminated hypotheses:** Harness orphan (READY but not in queue) — disproven by both the wedge detector and the `[ORPHAN-NEXT]` trace. Harness-preemptibility (`harness_nonpreempt` helped but did not fix).
 - **Next step (postponed):** Add a `wake_waiting_parent`/`restore_pod` serial trace to capture which waker fails. Or: run the `memory` class on a checkpoint of the scheduler rework with `harness_nonpreempt` and deferred-switch timing randomization to narrow the window. If reproduced on a different test class, re-evaluate.
 - **Severity:** High (intermittent, blocks `memory` class on ~1/3→~1/10 runs; may affect `all`).
 - **Domain:** Kernel — Scheduler / ready queue / harness wakeup.
 - **Status:** Open (postponed — investigation paused Jul 2026). Filed 2026-07-20 after #020 #PF fixes landed.


## Config‑Matrix Bugs – 2026-07-13 10:17:56
- **DMON0_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO0_DACT1.log
- **DMON0_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO1_DACT1.log
- **DMON0_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO0_DACT1.log
- **DMON0_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO1_DACT1.log
- **DMON1_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO0_DACT1.log
- **DMON1_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO1_DACT1.log
- **DMON1_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO0_DACT1.log
- **DMON1_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO1_DACT1.log

