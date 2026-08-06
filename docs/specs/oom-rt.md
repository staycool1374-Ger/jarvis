# OOM / Resource-Exhaustion RT-Safety Specification

**Semantics:** contracts for guaranteeing that admission-control, allocation
failure, and WCET behave gracefully under memory/resource exhaustion.
Synthesis of `_archive/oom-rt-safety-plan.md`, `_archive/wcet_analysis.md`, the
`CONFIG_MEMORY_BUDGET` mechanism, and the v0.3.12 Alloc/Free return-value audit
(ROADMAP).  `[IMPLEMENTED]` = code-verified.

## 1. The Problem (semantic)

A high-priority task that fails to allocate can monopolize the CPU (retry/spin)
and starve lower-priority tasks.  Every allocation failure must (a) be detected
at the call site, (b) propagate a defined error, and (c) never leave the
scheduler/allocator in a corrupt state.

## 2. Admission Control (`CONFIG_MEMORY_BUDGET`) [IMPLEMENTED, default OFF]

```
 init_memory_budget(total_pages)  (global budget after PMM init)
        │
 TaskControlBlock::create()
        │ deduct CONFIG_DEFAULT_STACK_PAGES from global budget
        │ insufficient → return nullptr (OOM, admission denied)
        ▼
 task runs ──▶ PMM::alloc_page()/alloc_contiguous() check task budget
        │ over budget → return 0 (+ optional OOM handler)
        ▼
 TaskControlBlock::cleanup() returns pages to the budget
```
- `CONFIG_MEMORY_BUDGET` default 0 (disabled) — current behavior preserved.
- `memory_budget_pages_`/`memory_used_pages_` on the TCB (set 0 at create).
- **`clone_kernel_pml4` rollback [IMPLEMENTED]:** a real
  `JARVIS_TEST(vmm_clone_failure_rollback)` replaces the historical STUB-8;
  the fork path must roll back cleanly on OOM (no partial PML4).

## 3. Allocation-Failure Contract (v0.3.12 audit)

**`ENSURE()` panics unconditionally; `PMM::free_page()` silently no-ops on
double-free (so a double-free pushes the same page onto the free list twice —
corruption with no diagnostic).**

Binding rules for every alloc/free site:
1. **Never ignore an alloc return.** An unchecked `PMM::alloc_*()` return that
   feeds a deref/map is a NULL/0-deref (v0.3.12 CRITICAL items).
2. **Free is idempotent-but-tracking.** A double-free must be diagnosed, not
   silently re-pushed.
3. **OOM handler callbacks** release the allocator lock before invoking the
   handler and re-acquire for retry.

### v0.3.12 open items (not yet landed)
- **A1** `init_kstack_window` (task.cpp:344/355/366): unchecked
  `alloc_page_table()` → on OOM writes phys 0 + maps it present+write.  Guard
  each alloc.
- **A2** `Scheduler::init` (scheduler.cpp:421): unchecked `create(idle)`
  → null deref of `idle_task_->state`.
- **A3** `Scheduler::reap` (scheduler.cpp:1448): unchecked re-create of idle
  + old idle freed even on failure → NO idle task.  Guard + keep old idle.
- **A4** `map_page_in_pml4` RV64 (vmm.cpp:478/492): unchecked alloc.

## 4. WCET / Bounding Contract

- No `UINT64_MAX` pause-spin in kernel paths reachable by tasks; blocking uses
  BLOCKED + `reschedule()` (see `specs/ipc.md` VULN-W2).
- `sys_receive` has a `timeout_ticks` variant (VULN-W3, `specs/boundary.md`).
- Every per-tick scheduler pass is bounded O(n_tasks) / O(1) bitmap ops.

## 5. Gaps

- **`memory_determinism` test class** (exhaust a pool/budget, verify
  policy-defined failure) — phase-3 item, verification status unconfirmed.
- **Double-free diagnostic** — no poisoned-free-list detection yet.
