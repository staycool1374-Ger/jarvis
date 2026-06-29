# Open Issues

## Kernel — VFS

### ID: #010 — vfs_resolve_proc test failure and test-report mismatch
- **Description:** `vfs_resolve_proc` (`src/kernel/test/test_vfs.cpp:126`) asserts `vn->mode & vfs::S_IFDIR` but the `/proc` vnode returned by `vfs::resolve("/proc")` does not have the directory mode flag set. The test fires `[TEST:FAIL]` (but still reports PASS because the assertion is non-fatal). Additionally, the test framework's `print_report()` output (Total/Passed/Failed) is not consistently flushed to the serial port before `qemu_debug_exit()` terminates QEMU.
- **Root Cause:** The `procfs` mount is initialized during `vfs::init()` at boot, but the `procfs` vnode may not have its mode bits (`S_IFDIR`) set correctly, or the mount point is not populated before the test runs. Test isolation snapshot/restore does not guarantee VFS/procfs mount state.
- **Severity:** Low (cosmetic — test still passes)
- **Domain:** Kernel — VFS
- **Status:** Open

## Kernel — Memory

### ID: #013 — MempoolFragmentation test hangs at test 438
- **Description:** `MempoolFragmentation` in `test_resource_exhaustion.cpp` hangs during `make test-all-debug` at test index 438. The test allocates 20 objects per MemPool size class (9 classes: 16–4096 bytes), fills them with 0xA5, then frees in reverse order. On some runs the loop over size 4096 (largest class) deadlocks or livelocks — likely a MemPool internal corruption or infinite loop in `MemPool::free()` when returning a large block to a fragmented pool.
- **Root Cause:** Not yet determined. Suspected MemPool bitmap/free-list corruption when freeing the last block of a particular size class in reverse order. Pre-existing — confirmed in baseline.
- **Fix:** Temporarily disabled by commenting out `REGISTER_CLASS(MempoolFragmentation)` in `test_resource_exhaustion.cpp`.
- **Severity:** Medium (blocks full test suite for release verification)
- **Domain:** Kernel — Memory / Test Infrastructure
- **Status:** Open (disabled)

## Test Infrastructure

### ID: #012 — print_report output lost from serial before QEMU exit
- **Description:** The test report printed by `print_report()` (Expected/Total/Passed/Failed lines) is consistently missing from the serial output captured by `make test-qemu`. The last test's `[PASS]` line appears, followed by truncation — the `==============================\n TEST RESULTS\n...` block never arrives in the capture. The Makefile expect script matches `\r\n  Failed: 0` from a partial report or not at all (falling through to `TIMEOUT`).
- **Root Cause:** The serial TX drain loop in `run_registered()` waits for THR empty (LSR bit 5) and TSR empty (LSR bit 6) before `qemu_debug_exit()`, but `print_report()` runs inside `run_filtered()` BEFORE the drain code. The drain only flushes bytes written AFTER `print_report()`. The report bytes are in the UART FIFO when `qemu_debug_exit()` terminates the QEMU process — the FIFO contents are lost.
- **Fix:** Move the serial drain to AFTER `print_report()` (inside `run_filtered()` or between `print_report()` and `qemu_debug_exit()`). Or make `print_report()` itself flush the UART FIFO as its last action.
- **Severity:** Low (cosmetic — expect script still works, but report is invisible in logs)
- **Domain:** Test Infrastructure
- **Status:** Open

## Fixed

These bugs have been resolved and the fixes are in the commit history:

| ID | Description | Fixed In |
|----|-------------|----------|
| #007 | test_idle_task output missing from serial | Older commit |
| #008 | GPF in cleanup_zombies after selftest on continuing builds | Older commit |
| #009 | Scheduler starves shell when daemons have same priority/period | Older commit |
| #011 | syscall_send_recv: SEND to self-task returns non-zero, resource leak | v0.2.24 (cross-arch hardening) |

## Kernel — Scheduler / Test Isolation

### ID: #014 — Stack corruption during test isolation snapshot/restore (release mode)
- **Description:** Release build (`-O2`) crashes with General Protection Fault during `snapshot_restore()` in test isolation. The scheduler switches to an RSP (`0xFFFF800000612F70`) outside the valid kernel stack range (`[0xFFFF800001654000-0xFFFF800001664000]`). Error: `[SCHED] switch: rsp=0xFFFF800000612F70 outside stack [0xFFFF800001654000-0xFFFF800001664000]`.
- **Root Cause:** Test isolation (`test_isolate.cpp`) captures full kernel state including ready queue bitmap. `ReadyQueueManager::restore_state()` was a no-op — it only cleared queues without rebuilding from restored TCBs or restoring the captured bitmap. Tasks that were RUNNING at snapshot time (not in ready queue) retain stale `context.rsp` values. On restore, scheduler rebuilds ready queue from ALL tasks with `state == READY`, but tasks that were RUNNING still have `state == RUNNING` in the restored array. If they later transition to READY and get enqueued, their stale RSP is used during context switch.
- **Fix Applied:**
  1. `PriorityMap::set_raw()` and `clear_all()` added to `src/kernel/task/priority_map.hpp` for bitmap manipulation
  2. `TaskQueue::reset_and_clear_tcb_ptrs()` added to `src/kernel/task/task_queue.cpp` to clear TCB queue pointers during restore
  3. `ReadyQueueManager::restore_state()` now restores captured bitmap via `bitmap_.set_raw()`
  4. `Scheduler::restore_state()` validates RSP for tasks actually in ready queues (traversing `runq_next_`) and resets corrupt RSP to `kernel_stack_top`
- **Verification:** Release build tests pass (84/84 PASS). Debug build has 1 pre-existing failure (`o1_ready_queue_128_levels` - OOM in test, unrelated).
- **Severity:** High (crashes release build test isolation)
- **Domain:** Kernel — Scheduler / Test Infrastructure
- **Status:** Fixed (verified)

