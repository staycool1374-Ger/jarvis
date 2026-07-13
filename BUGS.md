# Open Issues

## Kernel — Memory

### ID: #013 — MempoolFragmentation test hangs at test 438
- **Description:** `MempoolFragmentation` in `test_resource_exhaustion.cpp` hangs during `make test-all-debug` at test index 438. The test allocates 20 objects per MemPool size class (9 classes: 16–4096 bytes), fills them with 0xA5, then frees in reverse order. On some runs the loop over size 4096 (largest class) deadlocks or livelocks — likely a MemPool internal corruption or infinite loop in `MemPool::free()` when returning a large block to a fragmented pool.
- **Root Cause:** Not yet determined. Suspected MemPool bitmap/free-list corruption when freeing the last block of a particular size class in reverse order. Pre-existing — confirmed in baseline.
- **Fix:** Temporarily disabled by commenting out `REGISTER_CLASS(MempoolFragmentation)` in `test_resource_exhaustion.cpp`.
- **Severity:** Medium (blocks full test suite for release verification)
- **Domain:** Kernel — Memory / Test Infrastructure
- **Status:** Open (disabled)

## Fixed

These bugs have been resolved and the fixes are in the commit history:

| ID | Description | Fixed In |
|----|-------------|----------|
| #019 | `all`-suite `add_task` ENSURE fails: `task.state == TaskState::READY` (state=4/TERMINATED) for a freshly `create()`d task whose TCB block aliases a stale `tasks_[]`/`id_table_` entry from a prior freed task (use-after-free). INTERMITTENT — only reproduces in `all` mode (accumulated scheduler state), never standalone `spinlock` class. Verified: `create()` block is NOT in `tasks_[]` at alloc time (`CREATE-UAF` check) and no `TERMINATED` writer (scheduler `terminate`/worker/`cleanup_test_tasks`/deferred-kill/kernel fault handlers/syscall/daemon) is the culprit — the 4 is written in the micro-window before `add_task`'s first read. Suspicion: dangling `current_index_` (past `task_count_`) returned by `current_task()` feeds a fault handler that terminates the aliased block; separate ready-queue GPF in `TaskQueue::pop_front` (task_queue.cpp:29) also observed. Fix candidate: `TaskControlBlock::cleanup()` → `Scheduler::unregister_task()` (try_lock + idempotent removal) so every TCB free unregisters; `remove_task` made idempotent; `operator delete` calls `remove_task`. Root cause still open as of investigation. | investigating |
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

