# Open Issues

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

