# Open Issues

## Test Quality Issues

### ID: #007 — test_idle_task output missing from serial
- **Description:** Individual test names from `test_idle_task.cpp` (e.g. `idle_task_created_at_boot`, `idle_task_integrity_markers`) do not appear in the QEMU serial output captured by `make test-qemu`. Only the registration line `Registering idle task tests` is visible. The tests themselves pass (exit code 0) and their symbols exist in the ELF, but their `[PASS]`/`[FAIL]` lines are not emitted or get lost in serial output. Root cause unknown — possibly serial buffer interleaving with terminal escape sequences during test isolation snapshot/restore.
- **Severity:** Low (cosmetic/observability)
- **Domain:** Test Infrastructure
- **Status:** Fixed (tests now appear in output)

## Kernel Crashes

### ID: #010 — vfs_resolve_proc test failure and test-report mismatch
- **Description:** `vfs_resolve_proc` (`src/kernel/test/test_vfs.cpp:126`) asserts `vn->mode & vfs::S_IFDIR` but the `/proc` vnode returned by `vfs::resolve("/proc")` does not have the directory mode flag set. The test fails with `[FAIL]`. Additionally, the test framework's `print_report()` output (Total/Passed/Failed) is not consistently flushed to the serial port before `qemu_debug_exit()` terminates QEMU — the Makefile expect script's `-ex "\r\n  Failed: 0"` pattern may either miss a `Failed: N` line entirely or match prematurely, causing the Makefile exit code to not reflect the actual test-failure count.
- **Root Cause:**
  1. **vfs_resolve_proc:** The `procfs` mount is initialized during `vfs::init()` at boot, but the `procfs` vnode may not have its mode bits (`S_IFDIR`) set correctly, or the mount point is not populated before the test runs. The test isolation snapshot/restore saves/restores MemPool and scheduler state but does not guarantee VFS/procfs mount state — the `procfs` vnode mode field may be zeroed by the restore.
  2. **report flush:** `print_report()` writes via `Logger::raw_write()` → `serial_putc()`, which waits for THR empty for each byte. However, `qemu_debug_exit()` calls `outw(0x501, code)` immediately after `print_report()` returns, triggering QEMU `exit()`. The UART's 16-byte FIFO plus shift register may still hold in-flight bytes that are never transmitted.
- **Fix:**
  1. **vfs_resolve_proc:** Ensure `procfs` root vnode has `S_IFDIR` set in its mode field during `procfs` mount. Alternatively, skip the mode check and only verify the vnode is non-null (the test's primary purpose is resolving `/proc` successfully).
  2. **report flush:** Added serial TX drain loop in `run_registered()` (`src/lib/test.cpp`) — waits for THR empty (LSR bit 5) and TSR empty (LSR bit 6) before calling `qemu_debug_exit()`. Updated expect pattern to `-ex "\r\n  Failed: 0"` (exact match with CRLF line prefix).
- **Severity:** Medium (one test fails in every `all` run; report-flush is cosmetic)
- **Domain:** Kernel — VFS / Test Infrastructure
- **Status:** Open — vfs_resolve_proc root cause not yet fixed; serial drain mitigation applied

## Kernel Crashes

### ID: #008 — GPF in cleanup_zombies after selftest on continuing builds
- **Description:** Running `selftest` via shell in a build that continues after tests (e.g. `make run-release`) triggers a General Protection Fault in `cleanup_zombies` after all 84 tests pass. The test report prints `Failed: 0`, then daemons die/restart, then GPF at `rip=0xFFFF80000026A511` with `RAX=0xFF80000027868FFF` (non-canonical corrupted `shell_task_ptr_`). Vector `0xD`, error `0x0`.
- **Root Cause:** A timer interrupt nests inside the syscall handler's `sti()`/`hlt()`/`cli()` IPC window (in `sys_receive`), calling `rate_monotonic_schedule()` → `switch_to_task()`, which overwrites scheduler globals (including `shell_task_ptr_`) while the outer `reschedule()` is mid-operation. The ISR context-switch logic in `isr_stubs.asm` was consuming `scheduler_save_rsp_to`/`scheduler_load_rsp_from` even for nested interrupts (`isr_nesting_depth > 1`), corrupting state set up by the outer ISR.
- **Fix:** In `src/kernel/arch/x86_64/isr_stubs.asm`, skip the context-switch logic when `isr_nesting_depth > 1`. Only the outermost interrupt performs the context switch.
- **Severity:** High (crash on release/continuing builds)
- **Domain:** Kernel — Scheduler / ISR
- **Status:** Fixed (commit on main branch)

### ID: #009 — Scheduler starves shell when daemons have same priority/period
- **Description:** After Bug #008 fix, vfsd (PID 5) and iocd (PID 6) daemons prevent the shell from running. Both daemons have `priority=1, period_ticks=10`, shell has `priority=1, period_ticks=20`. `Scheduler::next_task()` used `task != current` as a tiebreaker for equal-priority/period tasks, causing vfsd and iocd to ping-pong infinitely. Shell never gets CPU.
- **Root Cause:** The `task != current` hack in `next_task()` (line 143-144 of scheduler.cpp) was designed to prevent selecting the current task, but when the current task has the highest priority/period, it finds the *other* task as `best`, causing a context switch on every tick.
- **Fix:** 
  1. Changed `next_task()` tiebreaker: when priority/period equal, prefer task with fewer `remaining_ticks` (been waiting longer). If both have same remaining ticks and current is best, pick the other for round-robin.
  2. Changed shell from `priority=1, period=20` → `priority=2, period=5` (rate-monotonic correct: shortest period = highest priority).
  3. Added Liu-Leyland LUB admission test in `add_task()`.
- **Severity:** High (shell unusable after boot)
- **Domain:** Kernel — Scheduler
- **Status:** Fixed (current session)

### ID: #011 — syscall_send_recv: SEND to self-task returns non-zero, resource leak
- **Description:** `syscall_send_recv` in `src/kernel/test/test_syscall.cpp:450` asserts `ret == 0` after `Syscall::handle(SEND, self_task_id, 0, 42, 0, nullptr)` but the syscall returns non-zero. The test creates a throwaway task, sets it as current, sends a message to itself, then receives it back. The failure occurs on the SEND step, before any RECEIVE. Additionally, the test leaks resources (PMM pages, MemPool blocks, Tasks, MessageQueues, Notify objects, EventGroups) because `remove_task()`/`cleanup()`/`delete` may not fully release IPC-related resources allocated during the SEND.
- **Root Cause:** Not yet determined. Possible causes:
  1. `SYS_SEND` may require the target task to have an active IPC endpoint or a specific task state (READY/RUNNING). The throwaway task was just created and added — its IPC structures may not be fully initialized.
  2. The `set_current()` swap changes which task's IPC permissions apply. The SEND syscall may validate the caller's identity against the target, and the artificial `set_current()` bypasses normal scheduling.
  3. The test creates a task with a no-op function body (`[](){}`) that immediately exits — by the time SEND reaches it, the task may have already terminated.
- **Severity:** Medium (one test fails, masked by earlier hang until now)
- **Domain:** Kernel — IPC / Syscall
- **Status:** Open — pre-existing, unmasked by Mutex deadlock fix

### ID: #012 — print_report output lost from serial before QEMU exit
- **Description:** The test report printed by `print_report()` (Expected/Total/Passed/Failed lines) is consistently missing from the serial output captured by `make test-qemu`. The last test's `[PASS]` line appears, followed by truncation — the `==============================\n TEST RESULTS\n...` block never arrives in the capture. The Makefile expect script matches `\r\n  Failed: 0` from a partial report or not at all (falling through to `TIMEOUT`).
- **Root Cause:** The serial TX drain loop in `run_registered()` waits for THR empty (LSR bit 5) and TSR empty (LSR bit 6) before `qemu_debug_exit()`, but `print_report()` runs inside `run_filtered()` BEFORE the drain code. The drain only flushes bytes written AFTER `print_report()`. The report bytes are in the UART FIFO when `qemu_debug_exit()` terminates the QEMU process — the FIFO contents are lost.
- **Fix:** Move the serial drain to AFTER `print_report()` (inside `run_filtered()` or between `print_report()` and `qemu_debug_exit()`). Or make `print_report()` itself flush the UART FIFO as its last action.
- **Severity:** Low (cosmetic — expect script still works, but report is invisible in logs)
- **Domain:** Test Infrastructure
- **Status:** Open
