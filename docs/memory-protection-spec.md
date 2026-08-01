# Memory Protection Requirement Specification — Jarvis RTOS

- **Status:** Draft (Requirement Spec — no implementation)
- **Target version:** `0.4.x` (new Memory-Protection sub-phase, precedes/parallels SMP)
- **Derivation:** Investigated from `docs/stack-guard-spec.md`, `docs/privilege_audit.md`,
  `docs/memory-subsystem-audit-fix.md` (VULN-004), `docs/kstack-window-pt-pool.md`,
  `docs/fork-pt-deep-copy.md`, and the current code in
  `src/kernel/memory/{pmm,vmm}.cpp`, `src/kernel/task/task.cpp`.
- **Spec file:** `docs/memory-protection-spec.md`
- **Referenced from:** `ROADMAP.md` §0.4.x

---

## 1. Normative Requirements

These are the requirements under specification, restated from the request with
precise semantics.

- **REQ-MP-01 (kernel/user separation).** The address-space layout shall
  provide *complete* isolation between the kernel domain and the user domain
  for **text, data, and stack** segments. A user task must never be able to
  read or write any kernel text/data/stack. A kernel task must always be able
  to read (and, where correct, write) any user task text/data/stack.

- **REQ-MP-02 (kernel-task ↔ kernel-task isolation).** Each **kernel task**
  shall have a private view of the kernel domain such that it cannot access
  the text, data, or stack of *another* kernel task. (Decided scope: **full
  private kernel-half page tables** per kernel task — see §5.)

- **REQ-MP-03 (user-task ↔ user-task isolation).** Each **user task** shall
  have a private view of the user domain such that it cannot access the text,
  data, or stack of *another* user task. (Already substantially implemented;
  this spec locks it in as a hard requirement and adds verification.)

- **REQ-MP-04 (kernel → user access).** A kernel task may always access user
  task data (text/data/stack), via the kernel direct map / appropriate CR3,
  regardless of which user task owns it.

- **REQ-MP-05 (user → data only via kernel API).** A user task may access
  memory *only* through the kernel API (syscalls). No user-accessible mapping
  may expose kernel memory, another task's memory, or device MMIO. Hardware
  enforcement (SMAP/SMEP on x86_64, PAN/PXN on arm/aarch64, analogous on
  riscv) is **recommended but not mandatory** (see §5).

- **REQ-MP-06 (per-task canary-protected segments).** Every task — kernel or
  user — shall have **canary-protected** text, data, and stack segments.
  "Canary-protected" = **both** MMU guard/red-zone pages *and* software
  sentinel canaries (see §5).

---

## 2. Current-State Investigation (findings)

All references are to the current `main` tree.

### 2.1 User↔user isolation — PRESENT (REQ-MP-03 satisfied in principle)
- Each user task owns a private PML4: `VMM::clone_kernel_pml4()`
  (`src/kernel/memory/vmm.cpp:551`) clones the kernel PML4 and then the user
  half is populated independently.
- `fork()` performs a true deep copy: `TaskControlBlock::clone()` calls
  `VMM::deep_copy_user_pages()` (`src/kernel/task/task.cpp:994`), allocating
  **new** PDPT/PD/PT and **new** data pages, copying contents. Parent and
  child are fully independent; `page_table_shared_ = false`.
- Physical ownership tracking: PMM maintains a user/kernel owner bitmap.
  `VMM::map_page_in_pml4()` enforces it with `ENSURE(PMM::is_user_page(...))`
  at `vmm.cpp:476` and `vmm.cpp:532` (VULN-004, committed). A kernel-owned
  page can never be mapped user-accessible.

### 2.2 User-stack guard page — PRESENT
- `create_user()` maps the user stack at `mem::STACK_VADDR + PAGE_SIZE`
  (`task.cpp:711`), leaving the page at `STACK_VADDR` **unmapped** → a guard
  page below every user stack. `clone()` does the same (`task.cpp:1029`).
- Kernel-stack guard page: production tasks route through the kslot window
  (`task.cpp:530`), which reserves one unmapped page below each kernel stack
  (`docs/kstack-window-pt-pool.md`). Test-active tasks use HHDM (documented
  exemption, `task.cpp:514-528`).

### 2.3 Kernel-task ↔ kernel-task isolation — ABSENT (REQ-MP-02 gap)
- All kernel tasks run in the **single shared kernel PML4** (`kernel_pml4_`),
  and the kernel half is the 1:1 HHDM direct map
  (`HHDM_OFFSET + phys`). Any kernel task can therefore dereference any other
  kernel task's stack, TCB, or static data at will. There is **no** kernel-
  domain page-table isolation between kernel tasks today.
- This is the single largest gap in the requirement set.

### 2.4 Kernel → user access — EFFECTIVELY PRESENT (REQ-MP-04 satisfied)
- The HHDM direct map covers all physical memory, so a kernel task can always
  reach any user physical page (e.g. `VMM::map_page_in_pml4` walks user pages
  via HHDM in `deep_copy_user_pages`). REQ-MP-04 needs no new mechanism;
  it must simply not be broken by REQ-MP-02.

### 2.5 User → kernel only via API — PRESENT at page-table level (REQ-MP-05 partial)
- User PML4 half contains only `PAGE_USER` mappings; kernel half is cloned
  but the *kernel half is not user-accessible* (no `PAGE_USER` on kernel
  mappings). The syscall entry (`syscall_entry.asm`) is ring-0. So a user
  task cannot name kernel memory directly.
- **Not yet hardened** by SMAP/SMEP/PAN/PXN. A kernel bug that accidentally
  dereferences a user pointer *in kernel mode* is not CPU-blocked. (This is
  why REQ-MP-05 marks HW enforcement as recommended-but-not-mandatory.)

### 2.6 Per-segment canaries — PARTIAL (REQ-MP-06 gap)
- **MMU guard pages:** present for *stacks* (kernel via kslot, user via
  `STACK_VADDR` red zone). **Not** present as red zones between text/data/bss
  or around heap (program break) segments.
- **Software sentinels:** not implemented anywhere. No magic-word canary is
  placed at segment boundaries and checked on syscall/context-switch entry.
- The `stack-guard-spec.md` §7 "software canary" alternative was considered
  but not adopted; only MMU guard pages exist.

---

## 3. Gap Analysis Matrix

| Req | Current state | Gap | Severity |
|-----|---------------|-----|----------|
| MP-01 | User→kernel blocked by page tables; kernel→user via HHDM | Kernel/user split OK; needs HW-enforcement hardening (MP-05) | Medium |
| MP-02 | Shared kernel PML4/HHDM | **No kernel-task↔kernel-task isolation** | Critical |
| MP-03 | Per-task PML4 + deep copy + owner ENSURE | Largely done; needs locked-in verification + guard pages on all user segments | Low (verify) |
| MP-04 | HHDM direct map | None — must preserve under MP-02 | Low |
| MP-05 | Ring-3 page tables only | No SMAP/SMEP/PAN/PXN; accidental kernel-user deref not CPU-blocked | Medium |
| MP-06 | MMU guard page on stacks only | **No red zones on text/data/heap; no software canaries anywhere** | High |

---

## 4. Specification (normative detail)

### 4.1 Address-space model
- **Kernel domain:** upper half of the virtual address space (PML4 indices
  256–511 on x86_64). Contains kernel text/data/bss, the HHDM, device MMIO,
  and **per-kernel-task private data/stack regions**.
- **User domain:** lower half (PML4 indices 0–255). Per-user-task private.

### 4.2 REQ-MP-02 — Private kernel page tables per kernel task
Each kernel task shall be assigned its own kernel-half page tables (a private
clone of the shared kernel mapping) instead of the monolithic `kernel_pml4_`.
- On task creation, clone the kernel PML4 into a task-private PML4
  (`clone_kernel_pml4()` already exists and can seed this).
- The shared, read-only kernel **text** may be mapped identically (shared
  frames are fine — isolation is about *writable data/stack*, not code), but
  each kernel task's **data/bss/stack** must reside in **private,
  non-shared physical pages** reachable only through that task's PML4.
- CR3 is switched to the task-private PML4 on context switch (already done
  for user tasks via `page_table_`; extend the same path to kernel tasks).
- **MP-04 preservation:** the HHDM remains mapped in every task-private PML4,
  so kernel→user access is unaffected.
- Boot/kernel tasks that are *singletons* (idle, scheduler, daemons) may share
  the canonical kernel PML4 where isolation is not required, but the
  **mechanism** must support private tables for any kernel task.

### 4.3 REQ-MP-03 / MP-01 — Segment layout
- **Kernel task segments:** `{text, rodata} (shared, RO)`, `{data, bss}`
  (private per task), `{stack}` (private, kslot guard page retained).
- **User task segments:** `{text, rodata}`, `{data, bss}`, `{heap}`,
  `{stack}` — all private; retain `STACK_VADDR` red zone.

### 4.4 REQ-MP-06 — Canary-protected segments (both mechanisms)
**A. MMU guard/red-zone pages (hardware):**
- One unmapped guard page below every stack (already done — keep).
- Unmapped red-zone pages inserted between adjacent segments where VA layout
  permits (text↔data, data↔heap, heap↔stack), at minimum a guard page at the
  *top* (high-address end) of the stack and a red-zone below the lowest
  segment.
- These trap on first offending access (#PF) — deterministic, zero runtime
  overhead in the non-fault case.

**B. Software sentinels (defense-in-depth):**
- Place a magic-word canary (e.g. `0xC0DECAFE_C0DECAFE`) at each segment
  boundary (beginning and end of text/data/bss/heap/stack regions) at task
  creation.
- Verify canaries on **every syscall entry** and **every context-switch
  entry** (`switch_to_task` / `isr_common`). On mismatch →
  `panic("segment canary corruption: task=<id> seg=<name>")` (or a
  configurable hook).
- Canaries live in a read-only/protected region so a runaway write that
  crosses a segment boundary is caught even if it lands in mapped memory
  (which MMU guard pages alone would miss).
- Cost: a handful of `uint64_t` compares per switch — within WCET budget;
  must be benchmarked and logged in the test summary.

### 4.5 REQ-MP-05 — Kernel API enforcement
- Page tables: unchanged (user half is `PAGE_USER`-only; kernel half not
  user-accessible). **Hard requirement** that this never regresses (covered
  by VULN-004 `ENSURE`).
- **Recommended (not mandatory):** enable SMAP/SMEP (x86_64 `CR4.SMAP`/
  `CR4.SMEP`) and PAN/PXN (aarch64) so the CPU itself blocks any
  kernel-mode dereference of a user pointer, and any user-mode execute of
  kernel memory. If enabled, a transient user-pointer deref in kernel mode
  must be wrapped in explicit `stac()`/`clac()` (x86) or PAN toggle (arm).
- If HW enforcement is *not* enabled, the spec MUST still guarantee isolation
  purely through page-table permissions (already true) and document the
  residual risk.

### 4.6 Acceptance criteria
1. A kernel task writing to another kernel task's `data`/`bss`/private-stack
   address (reached via that task's *intended* VA, not HHDM) triggers #PF.
2. A user task writing to another user task's address triggers #PF.
3. A user task writing to any kernel address triggers #PF.
4. A kernel task can read another user task's data via HHDM (MP-04) — test
   must pass.
5. Overwriting a segment-boundary software canary is detected at the next
   syscall/context-switch and causes a controlled panic (not silent
   corruption).
6. SMAP/SMEP/PAN/PXN (if enabled) block an accidental kernel-mode user-
   pointer deref; explicit `stac/clac` regions still work.
7. All checks are zero-overhead in the non-fault path (guard pages) and
   bounded-cost (canaries) — measured WCET recorded.

---

## 5. Decisions Captured (clarifying answers)
- **MP-02 scope:** *Full private page tables* per kernel task (not
  best-effort, not software-only).
- **MP-06 canary meaning:** *Both* MMU guard pages **and** software sentinels.
- **MP-05 HW enforcement:** *Recommended, not mandatory* (SMAP/SMEP/PAN/PXN).

## 6. Implementation Phasing (0.4.x)
- **0.4.0-MP1 — Kernel-task private page tables:** clone kernel PML4 per
  kernel task; private data/bss/stack frames; CR3 switch path; preserve HHDM
  (MP-04). Builds on existing `clone_kernel_pml4()` + kslot infrastructure.
- **0.4.0-MP2 — Segment red zones:** extend MMU guard pages to text/data/heap
  boundaries for both kernel and user tasks.
- **0.4.0-MP3 — Software canaries:** sentinel placement + verification on
  syscall/context-switch entry; panic hook.
- **0.4.0-MP4 — HW enforcement (recommended):** SMAP/SMEP/PAN/PXN enablement
  with `stac/clac` (x86) / PAN-toggle (arm) audit of kernel user-pointer
  derefs.
- **0.4.0-MP5 — Verification suite:** cross-task write #PF tests (kernel→
  kernel, user→user, user→kernel), canary-tamper detection tests, HHDM
  kernel→user read test, SMAP/PAN negative tests (if enabled).

> Note: 0.4.x in `ROADMAP.md` is currently "SMP + Multicore". Memory
> protection is a prerequisite for safe SMP (per-core page tables, cache
> coloring) and should land **before or alongside** 0.4.1.

## 7. Risks / Open Items
- **R1 (MP-02 TLB/CR3 cost):** per-kernel-task CR3 switches add TLB flush
  cost. Must batch/invalidate selectively; re-audit WCET (ties to SMP PCID
  work in 0.4.5).
- **R2 (shared kernel text):** allowing shared RO text frames is fine; ensure
  no writable kernel global is accidentally shared across private tables.
- **R3 (snapshot isolation):** kslot + page-table-pool already survive
  `snapshot_restore`. Private kernel PML4s must integrate the same way
  (exclude private PT pages from PMM bitmap restore, or save/restore them).
- **R4 (daemons):** vfsd/iocd are user tasks today; confirm MP-02 targets
  only *kernel* tasks, so daemon isolation is already covered by MP-03.
- **R5:** WCET of software-canary checks under 0.4.x SMP scheduling must be
  re-measured (cache coloring interactions).

### 7.1 Prior art — stray-write detection (implemented diagnostic)
- **Source:** `docs/investigation-cumulative-corruption.md` (Attempts 1–11,
  "Ideas for catching the root cause" §1–4). That log documents the
  historical cumulative-corruption failures (0xDD-poisoned / `0x5F58...`
  garbage TCBs) and proposes MPU-guarded TCB pages, per-tick/context-switch
  canary checks, QEMU icount replay, and a **ring-buffer TCB write tracker**.
- **Implemented:** the ring-buffer write tracker (Idea #4) is now realized as
  `src/kernel/task/tcb_write_log.hpp` + `tcb_write_log.cpp`. A `TCB_WRITE`
  macro wraps critical field writes (`magic`, `id`, `state`, `kernel_stack`)
  at task-create / `clone` / `restore_task_fields` sites and records the last
  50 writes (seq, tcb addr, field offset, old/new value, caller) when built
  with `-DCONFIG_TCB_WRITE_LOG`. `dump_tcb_write_log()` is invoked from the
  corruption-detection paths (`remove_task` bad-magic branch,
  `cleanup()` poisoned-TCB skip) so the last modifications of a corrupted TCB
  and their callers are captured on detection.
- **Reuse for MP-06:** the same tracer complements the §4.4 software-canary
  verification — when a canary/tick check trips, dumping the write log
  pinpoints the offending writer instead of only the corruption site.
- **Caveat:** `CONFIG_TCB_WRITE_LOG` is OFF by default (the macro collapses to
  a plain assignment) so production/CI builds carry zero overhead. It is a
  debugging aid, not a runtime safety mechanism.
