# Jarvis RTOS — Technical Documentation

**Semantics:** the docs tree is a set of **binding specifications** extracted
from the design/audit papers.  Historical root-cause analyses, superseded
plans, and fully-fixed fix-records live in `_archive/` (source material,
retained for traceability).  Open issues are tracked in `ROADMAP.md` →
"Open Issues".

## Spec Documents

| Doc | Semantic (what it binds) |
|---|---|
| `specs/scheduler.md` | scheduler / ready-queue / priority / task-lifecycle contracts (R1-3, INV-1..7, move_priority, WEDGE, snapshot rebuild) |
| `specs/ipc.md` | message-queue IPC, `send_sync`, mutex/PCP, and the deferred-switch machinery contracts |
| `specs/memory.md` | PMM / MemPool / VMM / kstack guard / snapshot-isolation contracts + REQ-MP-01..06 |
| `specs/boundary.md` | syscall / VFS / ELF trust-boundary contracts (6 principles, VULN ledger) |
| `specs/oom-rt.md` | OOM admission control, allocation-failure contract, WCET bounding |
| `zombie-list-spec.md` | zombie / reaper lifecycle detail (referenced by `specs/scheduler.md` §6) |
| `debugging.md` | GDB/lldb tooling workflow (operational, not a spec) |

## Relationship Map

```
                    ┌──────────────────────────────────────┐
                    │          ROADMAP.md (active)          │
                    │  Open Issues · v0.3.12 · Future 0.4+ │
                    └───────┬──────────────────────┬───────┘
                            │                      │
        ┌───────────────────┼──────────────────────┼───────────────────┐
        ▼                   ▼                      ▼                   ▼
 ┌───────────────┐   ┌───────────────┐      ┌──────────────┐   ┌──────────────┐
 │ specs/        │   │ specs/        │      │ specs/       │   │ specs/       │
 │ scheduler.md  │◀──│ ipc.md        │───▶  │ memory.md    │◀──│ boundary.md  │
 │ (ready queue, │   │ (IPC/sync,    │       │ (PMM/VMM/    │   │ (syscall/VFS/│
 │  priority,    │   │  deferred-sw) │       │  stack/snap) │   │  ELF)        │
 │  lifecycle)   │   └──────┬────────┘       └──────┬───────┘   └──────┬───────┘
 └──────┬────────┘          │                      │                    │
        │                   │        ┌─────────────┴─────┐             │
        ▼                   ▼        ▼                   ▼             ▼
 ┌──────────────┐   ┌──────────────┐   ┌───────────────────────┐  ┌──────────────┐
 │ zombie-list  │   │ oom-rt.md    │   │ v0.3.12 audit         │  │ debugging.md │
 │ spec         │   │ (admission,  │   │ (alloc/free return-   │  │ (GDB/lldb)   │
 └──────────────┘   │  WCET)       │   │  value audit, A1-A4)  │  └──────────────┘
                    └──────────────┘   └───────────────────────┘
```

```
 Caller / cross-reference summary (who reads whom):

 Scheduler (specs/scheduler.md)
   ├─ reads: zombie-list-spec.md (termination)   specs/ipc.md (send_sync RQ rows)
   ├─ read-by: specs/ipc.md (deferred-switch), specs/memory.md (snapshot RQ)
   └─ read-by: ROADMAP.md Open Issues (H2, ss_deadline)

 IPC (specs/ipc.md)
   ├─ reads: specs/scheduler.md (INV-5, move_priority)
   └─ read-by: specs/boundary.md (VULN-W2/W3 blocking), specs/oom-rt.md (WCET)

 Memory (specs/memory.md)
   ├─ reads: specs/scheduler.md (snapshot rebuild), specs/oom-rt.md (budget)
   └─ read-by: specs/boundary.md (W^X map_page), specs/ipc.md (pool/owner)
```

## Source Material (archived)

`_archive/` holds the original papers the specs were extracted from, including
the master investigation logs (`investigation-cumulative-corruption.md`,
`ipc_blocking-analysis.md`) and the `ipc_blocking-c-baseline.log` watchdog dump.
They are retained for traceability and are **not** normative.

## Benchmarks / Generated

- `benchmarks/mandelbrot.md` — benchmark results.
- `doxygen/html/` — regenerable Doxygen output (67 MB, tool-generated).
