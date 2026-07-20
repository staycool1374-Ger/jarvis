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
 - **Description:** After the #020 #PF fixes (commit 2026-07-20), the `memory` class passes **45/45** on a clean run (~37 s) but **intermittently HANGS** (TIMEOUT, no TEST SUMMARY) at ~test 22 — i.e. right after `buffer_pool_exhaustion` (test 21/45). The `user-app` placeholder (ID=4) has already terminated by that point, and the hang occurs with **no #PF and no kernel panic** in the log, so this is a **scheduling deadlock / lost-wakeup**, not a memory fault. Observed 1/3 consecutive runs hanging; the other 2 ran 45/0.
 - **Root Cause:** Unconfirmed — strongly correlated with the large **uncommitted scheduler rework** currently in the working tree (`src/kernel/task/scheduler.cpp` +~452 lines, `ready_queue_manager.cpp`, `task_queue.cpp`, `scheduler.hpp`, plus `kernel.cpp` `Scheduler::terminate` / `scheduler_need_resched` changes). The hang is timing-dependent (only fires on certain preemption/tick orderings), consistent with a ready-queue accounting or `in_ready_queue_` flag inconsistency introduced by that rework. It is **distinct** from #020 (which was a #PF and is now fixed).
 - **Why it is not #020:** #020's signature was a #PF (`err=0x15` user fetch of kernel `.text`, or `err=0x0` Terminal write of a user VA). #021 produces **no fault at all** and hangs at a different point (test ~22, after the user-app exits) — a wedged scheduler, not a bad pointer dereference.
 - **Next step:** Investigate the scheduler rework in isolation (git-stash the #PF commits, keep only the scheduler changes, run `memory` repeatedly) to reproduce, then binary-search the rework hunks. Candidate suspects: `ready_queue_manager::in_ready_queue_` maintenance on `restore_pod`/`wake_waiting_parent`/reap, `rebuild_ready_queue` flag-clear, and `set_current` only removing `old` from the queue. Add a watchdog/heartbeat assert that fires if no task runs for N ticks.
 - **Severity:** High (intermittent, blocks `memory` class on ~1/3 runs; may affect `all`).
 - **Domain:** Kernel — Scheduler / ready queue.
 - **Status:** Open. Filed 2026-07-20 after #020 #PF fixes landed; not a regression of the #PF fix (reproduces with the scheduler rework present, #PF fixes absent).

## Fixed

These bugs have been resolved and the fixes are in the commit history:

| ID | Description | Fixed In |
|----|-------------|----------|
| #019 | `all`-suite `add_task` ENSURE fails: `task.state == TaskState::READY` (state=4/TERMINATED) for a freshly `create()`d task whose TCB block aliases a stale `tasks_[]`/`id_table_` entry from a prior freed task (use-after-free). INTERMITTENT — only reproduces in `all` mode (accumulated scheduler state), never standalone `spinlock` class. Verified: `create()` block is NOT in `tasks_[]` at alloc time (`CREATE-UAF` check) and no `TERMINATED` writer (scheduler `terminate`/worker/`cleanup_test_tasks`/deferred-kill/kernel fault handlers/syscall/daemon) is the culprit — the 4 is written in the micro-window before `add_task`'s first read. Suspicion: dangling `current_index_` (past `task_count_`) returned by `current_task()` feeds a fault handler that terminates the aliased block; separate ready-queue GPF in `TaskQueue::pop_front` (task_queue.cpp:29) also observed. Fix candidate: `TaskControlBlock::cleanup()` → `Scheduler::unregister_task()` (try_lock + idempotent removal) so every TCB free unregisters; `remove_task` made idempotent; `operator delete` calls `remove_task`. Root cause still open as of investigation. | investigating |
| #020 | `memory` class panics/intermittently fails — originally mis-diagnosed as a 0xDD use-after-free, but the DECISIVE re-diagnosis (captured fault frame) showed **TWO separate #PFs**, both now FIXED: **(1) user-mode instruction-fetch #PF** — `buffer_pool_*` tests create `create_user()` placeholder tasks and `add_task()` them READY; a timer tick dispatched a task that resumed at its **kernel-address lambda entry in user mode** (`CS=0x1B`, `err=0x15`, `RIP==CR2==entry`), a protection fault. Fixed by `configure_user_yield_entry()` (test_buffer_pool.cpp): maps a tiny user-exec **yield stub** (`xor eax,eax; syscall; jmp`) into the task's own page table and rewrites the saved frame RIP/RDI to that user VA, so a dispatched placeholder just yields cooperatively. **(2) Terminal/serial #PF (root cause #3)** — `Syscall::sys_write` passed the raw user buffer VA straight to `Terminal::write`, which dereferenced it in kernel mode (`CS=0x8`, `err=0x0`) → #PF. Fixed by copying the payload into a kernel bounce buffer via `safe_copy_from_user`, AND by making the x86_64 #PF handler honor `g_user_access_recover_ip` **in kernel mode** (it was previously gated behind `from_user`, so the copy's fault-recovery never fired and the kernel panicked). The `user-app` placeholder's `write` buffer (a stack local in an unmapped page) was also switched to a `.rodata` literal (userspace/user-app.c) so it no longer faults. After these fixes the `memory` class runs 45/45 with no #PF; a SEPARATE flaky scheduler HANG at ~test 22 was split out as #021. | 2026-07-20 #PF fixes (kernel.cpp recovery, syscall_handlers_fs.cpp sys_write, test_buffer_pool.cpp configure_user_yield_entry, userspace/user-app.c) |
| #007 | test_idle_task output missing from serial | Older commit |
| #008 | GPF in cleanup_zombies after selftest on continuing builds | Older commit |
| #009 | Scheduler starves shell when daemons have same priority/period | Older commit |
| #010 | vfs_resolve_proc test failure and test-report mismatch (procfs S_IFDIR) | Prior to v0.2.19 |
| #011 | syscall_send_recv: SEND to self-task returns non-zero, resource leak | v0.2.24 (cross-arch hardening) |
| #012 | print_report output lost from serial before QEMU exit | 8368df9 (destructor cleanup) |
| #014 | Stack corruption during test isolation snapshot/restore (release mode) | Verified — release builds pass 84/84 |
| #015 | wait command freezes shell (waited for ALL tasks, not just children) | parent_id-based child wait — cmd_wait excludes system/daemon tasks |
| #016 | Daemon tasks killed by reload_daemon_tasks after snapshot_restore (PID zeroed, double-free via reap_orphans) | test_isolate.cpp — restore PID after reload; skip healthy daemon tasks in kill loops |
| #017 | buffer_pool_list_integrity_after_unlink stack overflow (bool found[10] indexed by raw entry index, LIFO free list starts at MAX_BUFFERS-1) | test_buffer_pool.cpp — match entry indices to handles[] slot position |
| #018 | test_iocd.cpp PRE annotation missing iocd daemon dependency | test_iocd.cpp — "PRE: none" → "PRE: vfsd, iocd" |


## Config‑Matrix Bugs – 2026-07-13 10:17:56
- **DMON0_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO0_DACT1.log
- **DMON0_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO1_DACT1.log
- **DMON0_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO0_DACT1.log
- **DMON0_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO1_DACT1.log
- **DMON1_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO0_DACT1.log
- **DMON1_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO1_DACT1.log
- **DMON1_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO0_DACT1.log
- **DMON1_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO1_DACT1.log

