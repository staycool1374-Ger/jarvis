# Open Issues

## Test Quality Issues

### ID: #007 — test_idle_task output missing from serial
- **Description:** Individual test names from `test_idle_task.cpp` (e.g. `idle_task_created_at_boot`, `idle_task_integrity_markers`) do not appear in the QEMU serial output captured by `make test-qemu`. Only the registration line `Registering idle task tests` is visible. The tests themselves pass (exit code 0) and their symbols exist in the ELF, but their `[PASS]`/`[FAIL]` lines are not emitted or get lost in serial output. Root cause unknown — possibly serial buffer interleaving with terminal escape sequences during test isolation snapshot/restore.
- **Severity:** Low (cosmetic/observability)
- **Domain:** Test Infrastructure
- **Status:** Fixed (tests now appear in output)

## Kernel Crashes

### ID: #008 — GPF in cleanup_zombies after selftest on continuing builds
- **Description:** Running `selftest` via shell in a build that continues after tests (e.g. `make run-release`) triggers a General Protection Fault in `cleanup_zombies` after all 84 tests pass. The test report prints `Failed: 0`, then daemons die/restart, then GPF at `rip=0xFFFF80000026A511` with `RAX=0xFF80000027868FFF` (non-canonical corrupted `shell_task_ptr_`). Vector `0xD`, error `0x0`.
- **Root Cause:** Same mechanism as the nested-timer corruption addressed by the `isr_nesting_depth` fix — a timer interrupt nests inside the syscall handler's `sti()`/`hlt()`/`cli()` IPC window, calling `rate_monotonic_schedule()` → `switch_to_task()`, which overwrites scheduler globals (including `shell_task_ptr_`) while the outer `reschedule()` is mid-operation. In `make test-qemu` the system exits immediately after the test report via `qemu_debug_exit(0)`, so the corrupted memory is never dereferenced. In `make run-release` the system continues running after tests, triggering `cleanup_zombies()` during daemon restart, which reads the corrupted `shell_task_ptr_` and GPFs.
- **Severity:** High (crash on release/continuing builds)
- **Domain:** Kernel — Scheduler / ISR
- **Status:** Open — the `isr_nesting_depth` guard may have a gap or there is an additional path corrupting `shell_task_ptr_`
