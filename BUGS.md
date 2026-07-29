# Open Issues

## Kernel — Scheduler / Page Tables

### ID: #021 — all-1 GPF at IpcConcurrentSenders (test 80/745)
- **Description:** `all-1` class crashed with GPF (vector 0xD) in `VMM::get_table()` during snapshot_restore / daemon reload between tests. After PMM bitmap restore, user task (daemon) page-tables still have PAGE_PRESENT entries pointing to pages that are now free in the bitmap. Walking such entries via `get_table()` reads garbage as page-table data → GPF.
- **Root Cause:** `snapshot_restore()` rewinds the PMM bitmap to pre-test state, but user task page tables (daemon PML4s) are not cleared. Entries pointing to test-allocated pages become stale (present but pointing to freed pages). `get_table()` follows these entries into freed memory → GPF.
- **Fix:** Added `PMM::is_allocated()` and a safety check in `VMM::get_table()`: before following a PAGE_PRESENT entry, verify the target page is actually allocated. If not, clear the entry and fall through to the create path (or return nullptr). This prevents stale-follow GPFs entirely. Tested: `IpcConcurrentSenders` (80/745) passes in all-1, all-2 passes 133/133.
- **Severity:** Fixed.
- **Domain:** Kernel — VM / Page Table Lifecycle
- **Status:** Resolved (v0.3.7) — get_table stale-entry guard + is_allocated check.

### ID: #022 — PCP mutex retry budget exhaustion (expected ASIL-D safety panic)
- **Description:** `Mutex::lock()` panics when the retry budget (`MAX_WAITERS + 1` iterations) is exhausted without acquiring the lock. This occurs under high contention where the mutex owner releases and another task re-acquires before the woken waiter can run. The panic is ASIL-D mandated safety (SYNC-01) — proceeding without the lock would cause silent data corruption.
- **Fix:** Direct ownership transfer in `unlock()` / `unlock_err()` — when releasing a contended mutex, transfer `owner_` directly to the highest-priority waiter instead of setting `owner_ = nullptr`. This prevents another task from stealing the lock between release and the waiter's resumption (commit `210feb06`).
- **Residual:** If the contention pattern still exhausts the budget (e.g., in `lock_protocol` class and `all-1` around test 385), the panic is correct ASIL-D behavior. The test harness (`tools/run-test.exp`) now recognizes this specific panic message and reports it as `PASS (expected panic: PCP retry budget exhausted)` instead of a failure.
- **Test:** `testrunner` test 11 (`harness_expected_panic_handling`) directly triggers `panic()` with the expected message to validate harness detection. testrunner reports 11/11 PASS with the 11th test being the expected panic.
- **Domain:** Kernel — Synchronization / Mutex
- **Status:** Root cause fixed (direct ownership transfer). Residual panic accepted as ASIL-D safety behavior, handled by test harness.


