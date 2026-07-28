# Test Cases — v0.3.6 (Memory + Scheduler + IPC/Sync Audit Remediation)

## Branch: main

All 19 audit findings resolved and verified via existing test classes.
See `ROADMAP_done.md` for full commit log and `audits/` for source findings.

### Verification results (HEAD)
| Test class | Tests | Result |
|-----------|-------|--------|
| `selftest` | 132 | 132/132 PASS |
| `memory` | 47 | 47/47 PASS |
| `scheduler` | 55 | 55/55 PASS |
| `vmm` | 8 | 8/8 PASS |
| `ipc` | 42 | 42/42 PASS |
| `ipc_blocking` | 4 | 4/4 PASS |
| `ipc_robustness` | 6 | 6/6 PASS |
| `pmm` | 5 | 5/5 PASS |

### Known limitations
- `all-1` GPF at `IpcConcurrentSenders` (test 80/745) — shared page-table lifecycle issue, tracked as BUGS.md#021. Individual test classes pass cleanly.
