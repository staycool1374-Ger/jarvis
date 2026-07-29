# Open Issues

## Kernel — Scheduler / Page Tables

### ID: #021 — all-1 GPF at IpcConcurrentSenders (test 80/745)
- **Description:** `all-1` class crashed with GPF (vector 0xD) in `VMM::get_table()` during snapshot_restore / daemon reload between tests. After PMM bitmap restore, user task (daemon) page-tables still have PAGE_PRESENT entries pointing to pages that are now free in the bitmap. Walking such entries via `get_table()` reads garbage as page-table data → GPF.
- **Root Cause:** `snapshot_restore()` rewinds the PMM bitmap to pre-test state, but user task page tables (daemon PML4s) are not cleared. Entries pointing to test-allocated pages become stale (present but pointing to freed pages). `get_table()` follows these entries into freed memory → GPF.
- **Fix:** Added `PMM::is_allocated()` and a safety check in `VMM::get_table()`: before following a PAGE_PRESENT entry, verify the target page is actually allocated. If not, clear the entry and fall through to the create path (or return nullptr). This prevents stale-follow GPFs entirely. Tested: `IpcConcurrentSenders` (80/745) passes in all-1, all-2 passes 133/133.
- **Severity:** Fixed.
- **Domain:** Kernel — VM / Page Table Lifecycle
- **Status:** Resolved (v0.3.7) — get_table stale-entry guard + is_allocated check.

### ID: #022 — all-1 PCP mutex retry budget exhausted (test ~382/745)
- **Description:** `all-1` panics with `Mutex::lock() exhausted PCP retry budget` at approximately test 382 in the full 745-test sequence. The PCP mutex retry loop (`MAX_WAITERS + 1` iterations) exits without acquiring the lock. This occurs in the non-PCP path (the retry loop is shared); the panic message's "PCP" refers to the loop variable name.
- **Root Cause:** Under investigation. Likely a high-contention scenario where a mutex owner is preempted or delayed while holding the lock, and a waiter wakes up, re-adds itself, and blocks repeatedly without the lock being released. Timing-dependent — not reproducible in isolated class runs.
- **Severity:** Medium — blocks full all-1 completion; all individual test classes pass.
- **Domain:** Kernel — Synchronization / Mutex
- **Status:** Pre-existing (masked by #021 GPF which prevented all-1 from reaching test 382)


