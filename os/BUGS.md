# Open Issues

## Test Quality Issues

### ID: #007 — test_idle_task output missing from serial
- **Description:** Individual test names from `test_idle_task.cpp` (e.g. `idle_task_created_at_boot`, `idle_task_integrity_markers`) do not appear in the QEMU serial output captured by `make test-qemu`. Only the registration line `Registering idle task tests` is visible. The tests themselves pass (exit code 0) and their symbols exist in the ELF, but their `[PASS]`/`[FAIL]` lines are not emitted or get lost in serial output. Root cause unknown — possibly serial buffer interleaving with terminal escape sequences during test isolation snapshot/restore.
- **Severity:** Low (cosmetic/observability)
- **Domain:** Test Infrastructure
- **Status:** Fixed (tests now appear in output)
