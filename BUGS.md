# Open Issues

## Kernel — Scheduler / Page Tables

### ID: #021 — all-1 GPF at IpcConcurrentSenders (test 80/745)
- **Description:** `all-1` class crashes with GPF (vector 0xD) in `VMM::map_page_in_pml4` after `IpcConcurrentSenders` (80/745) PASSes. The crash happens during snapshot_restore / daemon reload between tests. `get_table()` returns a pointer to a previously-freed page-table page because a parent PML4/PDPT entry was not cleared after a sibling `free_user_pages()` freed the shared page.
- **Root Cause:** When multiple page tables share page-table-descriptor pages (PDPT/PD/PT via `clone_kernel_pml4` + manual copy), `free_user_pages()` on one table frees the shared pages. With VULN-004's alloc-time ownership model (freed pages retain USER owner bit), a subsequent `free_user_pages()` on a sibling table walks into the freed page because `is_user_page()` still returns true → GPF on garbage content.
- **Fix (partial):** `free_user_pages()` now clears parent entries (`pd[pd_idx] = 0`, `pdpt[pdpt_idx] = 0`) after freeing child page-table pages.  `get_table()` returns nullptr instead of asserting on OOM, with null checks added at all call sites.  The crash is mitigated but not fully eliminated in the 745-test `all-1` sequence — `IpcConcurrentSenders` passes 6/6 when run alone via `ipc_robustness`.
- **Severity:** Medium — only triggers in full `all-1` run at test 80; all individual test classes pass.
- **Domain:** Kernel — VM / Page Table Lifecycle
- **Status:** Under investigation (v0.3.7)

## Kernel — Memory

### ID: #013 — MempoolFragmentation test hangs at test 438
- **Description:** `MempoolFragmentation` in `test_resource_exhaustion.cpp` hangs during `make test-all-debug` at test index 438. The test allocates 20 objects per MemPool size class (9 classes: 16–4096 bytes), fills them with 0xA5, then frees in reverse order. On some runs the loop over size 4096 (largest class) deadlocks or livelocks — likely a MemPool internal corruption or infinite loop in `MemPool::free()` when returning a large block to a fragmented pool.
- **Root Cause:** Not determined — no longer reproducible after scheduler/ReadyQueue refactoring (v0.3.5). Passes 51/51 in `memory` class.
- **Fix:** Re-enabled test and registration (`test_resource_exhaustion.cpp`). Removed `#if 0` guard.
- **Severity:** Closed — test passes.
- **Domain:** Kernel — Memory / Test Infrastructure
- **Status:** Resolved (2026-07-26)

## Config‑Matrix Bugs – 2026-07-13 10:17:56
- **DMON0_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO0_DACT1.log
- **DMON0_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET0_SPO1_DACT1.log
- **DMON0_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO0_DACT1.log
- **DMON0_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON0_DMISS1_WCET1_SPO1_DACT1.log
- **DMON1_DMISS1_WCET0_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO0_DACT1.log
- **DMON1_DMISS1_WCET0_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET0_SPO1_DACT1.log
- **DMON1_DMISS1_WCET1_SPO0_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO0_DACT1.log
- **DMON1_DMISS1_WCET1_SPO1_DACT1**: FAIL – /tmp/jarvis_config_matrix_DMON1_DMISS1_WCET1_SPO1_DACT1.log

