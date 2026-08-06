# Jarvis RTOS — Development Roadmap

**Build:** v0.3.12-dev | **Last Release:** v0.3.7 | **Completed milestones:** see `ROADMAP_done.md` (v0.2.x — v0.3.11)

## Safety & Concurrency Guardrails (Strict)
- **Transition to Fine-Grained Locks:** All new synchronization code must use `SpinLock` + `SpinLockGuard` for short critical sections and `sync::Mutex` (without IrqGuard) for blocking paths. The global `IrqGuard` is deprecated for all uses except boot, panic, and test isolation.
- **Reference-Enforced Tasks:** When manipulating task blocks or IPC endpoints within the new init system or system calls, strictly enforce reference passing over raw pointers to prevent dangling lookups.
- **Zero-Allocation tmpfs Operations:** Ensure the initial `tmpfs` implementation relies on the pre-existing fixed `MemPool` / `BufferPool` infrastructure for its nodes to avoid unbounded allocations that violate resource tracking limits.

## Open Issues (known, not yet fixed)
- **Residual H2 deferred-switch race (v0.3.9):** the harness is intermittently displaced onto an orphaned page during a test's daemon wait; layers 4-6 (dispatch-guard, scratch-save, apply-side RSP-owner check) contain but do NOT eliminate it (~17-50% ipc flake; `all` can hang at test 77/78).  Full investigation log in `ROADMAP_done.md` §v0.3.9.  Needs a hardware-watchpoint session (QEMU gdb-stub watchpoints are broken; the `[H2W]` kernel recorder + `tools/gdb/h2_walk_pt.py` are the working instruments).
- **`ss_deadline` class (pre-existing):** an EXHAUSTED sporadic-server task at bg_prio 2 cannot be re-dispatched after `gate.post()`, so the harness's `while (state != TERMINATED)` spins forever.  Needs a dedicated test redesign.
- **`priority_inheritance` class (pre-existing):** hangs at test 1 `MutexPriorityDonates` — an INV-4 gate-spin test-code race in `spawn_holder`.

## Active Development — v0.3.12

### Alloc/Free Return-Value Audit — fix unhandled alloc/free results

Source: full audit of `src/kernel/**` for alloc/free call sites where the
return value is ignored, partially validated, or feeds a double-free/stale-free.
All findings below are VERIFIED against the code (2026-08-03); no fixes landed
yet.  `ENSURE()` panics unconditionally; `PMM::free_page()` silently no-ops on
double-free (so a double-free pushes the same page onto the free list twice —
corruption with no diagnostic).

#### (A) CRITICAL — unchecked alloc return → NULL/0 deref (fix first)

- [ ] **A1 — `init_kstack_window` (task.cpp:344, 355, 366):**
      `PMM::alloc_page_table()` return is unchecked, then
      `HHDM_OFFSET + pdpt_phys` is memset and `pml4[pml4_idx] = pdpt_phys|P|W`
      is installed.  On OOM: writes to physical page 0 and maps phys 0 as
      present+write in the live kernel PML4.  Reached lazily from the first
      `alloc_kslot()` → every task-creation path.  Fix: guard each alloc; on
      failure `panic()` (boot path) or return a non-fatal error.
- [ ] **A2 — `Scheduler::init` (scheduler.cpp:421-423):**
      `TaskControlBlock::create(idle_task_main, ...)` unchecked → `idle_task_->state`
      derefs nullptr on MemPool OOM.  Fix: null-guard before the deref.
- [ ] **A3 — `Scheduler::reap` (scheduler.cpp:1448-1458):**
      `create(idle_task_main, ...)` unchecked → `created->state` null deref AND
      the old idle TCB is freed anyway (`:1457`), leaving the system with NO
      idle task even if the deref is guarded.  Fix: guard `created`; do NOT
      free the old idle unless the replacement succeeded; keep `idle_task_`
      valid.
- [ ] **A4 — `map_page_in_pml4` RV64 (vmm.cpp:478, 492):**
      `get_table(..., true, true)` returns nullptr on OOM; both `l1` and `l2`
      unchecked → null deref (and `get_table(l1,...)` derefs null at vmm.cpp:122).
      This is the USER-mapping path (ELF load, brk, BufferPool).  Fix: null-check
      each level; return/fail on OOM.
- [ ] **A5 — `map_page` RV64 (vmm.cpp:222, 242, 253):** same unchecked
      `get_table` pattern in the kernel-mapping path.  Fix as A4.
- [ ] **A6 — `map_page` x86_64 (vmm.cpp:311, 327):**
      `pdpt`/`pd` are null-checked but the final `pt = get_table(pd, pd_idx, true)`
      is NOT → `pt[pt_idx] = phys|flags` null write on OOM.  Fix: null-check `pt`.

#### (B) HIGH / minor — ignored or partial validation

- [ ] **B1 (HIGH) — `IPC::send` (ipc.cpp:240):**
      `BufferPool::transfer(msg.buf_handle, *cur, *tcb)` return IGNORED.  On
      failure the message is still queued with a `buf_handle`; the receiver's
      `BufferPool::map()` validates index+generation but NOT owner → receiver
      can map a buffer still on the SENDER's list → both `buf_list_head` chains
      hold the same entry → list corruption / physical-page double-free.  Fix:
      check the transfer return; on failure drop the message or roll back the
      queued handle.
- [ ] **B2 — `exec_into_current` (elf.cpp:489-491):**
      on `!load_segments_and_stack(...)` the freshly cloned `new_pml4` and any
      partially mapped segments/heap/stack LEAK (contrast `:494-499` which frees
      them).  Fix: free `new_pml4` (and partial segments) before returning false.
- [ ] **B3 — `create_user` (task.cpp:696-708):**
      if `clone_kernel_pml4()` fails, `ustack_phys` was never stored in
      `tcb->user_stack_`, so `delete tcb → cleanup()` doesn't free it → physical
      page leak per failed user-task creation.  Fix: store the stack phys in the
      TCB (or free it) before the clone-failure return.
- [ ] **B4 — `virtio_net.cpp` probe (139, 150, 159, 174):**
      `VirtioNetDevice` has NO destructor freeing `rx_desc_phys/avail/used`,
      `tx_*`, `rx_bufs_phys[]` → each probe-failure branch leaks the pages
      allocated so far.  Fix: add a destructor (mirror `VirtioBlkDriver`).
- [ ] **B5 — `AhciDriver::port_init` (ahci.cpp:198-203):**
      if a later `alloc_contiguous` fails, earlier `ct_phys_[port][0..s-1]` pages
      (and the mapped CL/RFIS) are not freed, and `init_done_` stays false so
      `~AhciDriver` (which early-returns) never frees them either.  Fix: roll
      back already-allocated slots on failure.
- [ ] **B6 — ENSURE-on-OOM → panic (vmm.cpp:139/227/289, mempool.cpp:54,
      buffer_pool.cpp:173):** the return IS consumed but only to panic.  The
      `get_table` huge-page split is inconsistent with the rest of `get_table`
      (which returns nullptr).  Fix: return nullptr from the split path instead
      of `ENSURE`, and let callers handle it (after A4-A6).
- [ ] **B7 — `register_driver` (driver.cpp:40-45):** stores a nullptr in
      `drivers_[count_++]` on MemPool OOM (benign today — all consumers guard
      `!drv`).  Optional: skip the slot on alloc failure.

#### (C) FREE-path double-free / stale-free risks

- [ ] **C1 — `TaskControlBlock::cleanup` (task.cpp:1275):**
      `PMM::free_page(page_table_)` is NOT gated on `!page_table_shared_`
      (the `free_user_pages` call at `:1268-1270` IS gated).  If sharing is ever
      re-enabled, two tasks free the same PML4 page → silent double-free.
      Currently latent (deep-copy replaced shared page tables; scheduler.cpp:1435).
      Fix: gate `PMM::free_page(page_table_)` on `!page_table_shared_`.
- [ ] **C2 — `exec_into_current` (elf.cpp:563-567):** identical latent pattern —
      `PMM::free_page(old_pml4)` not gated on `old_shared`.  Fix as C1.
- [ ] **C3 — `BufferPool::alloc_page` (buffer_pool.cpp:190-195):**
      `pool_pages_[]` is NOT in `capture_state`/`restore_state` (only `entries`,
      `free_head_`, `next_cookie_`, `pool_count_`).  After a snapshot restore,
      `pool_count_` can point at stale entries whose pages were freed/recycled.
      `is_user_page(phys)` guard can't distinguish "stale but free" from "stale
      and re-allocated" → `PMM::free_page(phys)` may free a foreign page.
      Fix: include `pool_pages_[]` in the snapshot, or clear slots on free
      (see C4).
- [ ] **C4 — `BufferPool::free_page` (buffer_pool.cpp:209-219):** when the pool
      is full the page goes to PMM but the array slot is not scrubbed; stale
      entries persist and feed C3.  Fix: zero the slot after PMM-free.

**Required fix discipline (per AGENTS.md Mandatory Bugfix Sequence):**
1. Classify each: A = null-deref/OOM, B = leak/ignored-return, C = double-free
   (all memory-safety/logic, not timing).
2. Read the affected code + callers before editing (do not fix blind).
3. One hypothesis per item, validated by build + the smallest applicable test
   class (e.g. A4-A6 → `memory`/`pmm`; B1 → `ipc`; C1/C2 → `process`).
4. Implement, `make build` clean, run the class to 0 failures.
5. After all items: `make execute-test x86_64 debug all` — NOTE the H2 race
   (v0.3.9) may still hang at ~test 78; use per-class gates as acceptance and
   keep `CONFIG_DEBUG_IPC_SCHED` ON for the debug `all` gate.

**Acceptance criteria (DONE when):**
- A1-A6, B1, C1-C2 fixed (each verified by build + class gate).
- B2-B5 leak paths closed (no new ResourceTracker deltas in `elf`/`process`/
  `driver`/`vfs` classes).
- `make build` clean (check-style Errors: 0), `selftest` 132/132.
- `test-history.txt` rows appended for every class touched.

**Out of scope:** H2 race (v0.3.9), BufferPool +1
(v0.3.11), and ISO 26262 certification artifacts.


## Past Releases

See `ROADMAP_done.md` for completed items: v0.2.x — v0.3.11 (boundary audit, PfA concurrency redesign, test hygiene, H2 race, trigger-driven testing rework, BufferPool +1 PMM leak).

---

## Future Roadmap (Aspirational)

### Phase 4.5: Memory Protection (0.4.x) — prerequisite for safe SMP
- [ ] **Requirement spec:** `docs/specs/memory.md` §7 (REQ-MP-01..06). Current state: user↔user isolation + user-stack guard pages present; kernel-task↔kernel-task isolation ABSENT; software canaries absent. Decisions: full private kernel page tables, both MMU guard pages + software canaries, HW enforcement (SMAP/SMEP/PAN/PXN) recommended-not-mandatory.
- [ ] **0.4.0-MP1** — Private kernel-half page tables per kernel task (clone kernel PML4, private data/bss/stack frames, CR3 switch; preserve HHDM for kernel→user access)
- [ ] **0.4.0-MP2** — MMU red-zone guard pages between text/data/heap/stack segments (kernel + user tasks)
- [ ] **0.4.0-MP3** — Software sentinel canaries at segment boundaries, verified on syscall + context-switch entry
- [ ] **0.4.0-MP4** — Optional HW enforcement: SMAP/SMEP (x86_64) / PAN/PXN (aarch64) with `stac/clac` audit
- [ ] **0.4.0-MP5** — Verification suite: cross-task #PF tests, canary-tamper detection, HHDM kernel→user read, SMAP/PAN negatives
- [ ] **0.4.0-MP6** — Kernel stack guard page via private VA window (moved from v0.3.7; requires snapshot-safe page table pool)
- [ ] **0.4.0-MP7** — `page_table_shared_` removal — complete deep-copy fork (walk all user entries, allocate new PDPT/PD/PT, copy contents). Current state: config + pool done.

### Phase 5: SMP + Multicore (0.4.x)
#### 0.4.1–0.4.2 — APIC & SMP Boot
- [ ] Local/IO APIC, X2APIC, per-CPU GDT/TSS, INIT-SIPI AP startup
- [ ] TPR-based interrupt prioritization, core state isolation
- [ ] **Per-CPU asm for `isr_nesting_depth`** — move from the single global symbol to GS-relative access on x86_64 (TPIDR/tp on aarch64/riscv64); the C++ side already uses `__atomic_*` (v0.3.7 PfA-B). CpuContext plumbing (`current_cpu()`) is in place.
- [ ] **`hhdm_modified_` (VAR-17) re-audit** — task-context only today (single-core safe); re-audit under SMP with per-CPU ownership or atomics.

#### 0.4.3–0.4.4 — Per-CPU Scheduling & Cache
- [ ] Distributed run queues, real-time load balancer, SYS_SET/GET_AFFINITY
- [ ] Cache coloring allocator, SMP spinlocks/rwlocks, WCET re-audit

#### 0.4.5–0.4.6 — TLB Shootdown & IPI Reduction
- [ ] PCID, selective INVPCID, lazy shootdowns, IPI batching, latency profiling

### Phase 6: System Integration / Userspace ABI (0.5.x)

**Priority:** picolibc integration — syscall ABI, TLS, POSIX stubs.

#### Syscall ABI Definition
- [ ] **Document trap/IRQ numbers** — create `src/kernel/syscall/syscall.h` with stable, documented trap vectors and IRQ numbers
- [ ] **Register conventions** — specify register layout for syscall arguments and return values per architecture (x86_64: `rax=num, rdi, rsi, rdx, r10, r8, r9`; aarch64: `x8=num, x0-x5`; riscv64: `a7=num, a0-a5`)
- [ ] **syscall.h public header** — export to userspace, used by both kernel dispatcher and libc stubs

#### picolibc Integration
- [ ] **POSIX syscall stubs** — implement `src/libc/picolib_stubs.c` with wrappers for `_write`, `_read`, `_sbrk`, `_exit`, `_open`, `_close`, `_fstat`, `_lseek`, `_getpid`, `_kill` using `jarvis_syscall()` dispatcher
- [ ] **Build picolibc** — compile with meson as `libc.a` + `libm.a` (static), targeting x86_64-elf
- [ ] **Makefile integration** — link `libc.a`/`libm.a` into kernel image; add build rules for picolibc subproject
- [ ] **TLS on context switch** — every task switch must load the thread-local-storage address into the appropriate base register (`FS` on x86_64, `TPIDR_EL0` on aarch64, `tp` on riscv64). picolibc uses this for `errno` and per-task internal state — no global locks needed.
- [ ] **Verify** — `printf`, `malloc`, `scanf` work from userspace tasks via syscall stubs

### Phase 7: Safety Systems (0.6.x)
- [ ] ICH9/HPET hardware watchdog + NMI pre-timeout, PIT fallback, SYS_WATCHDOG_KICK
- [ ] Per-task software watchdog (SYS_WATCHDOG_CREATE), /proc/[pid]/watchdog
- [ ] Wait-for-graph deadlock detection, watchdog-driven recovery, SYS_HEALTH_STATUS
- [ ] Idle-task safety monitors: RAM March C-, CPU ALU verification, utilisation tracking

### Phase 8: Microkernel Transition (0.7.x–0.8.x)
- [ ] Externalise VFS & block I/O to user-space servers
- [ ] Externalise device drivers (keyboard, framebuffer, timer/RTC)
- [ ] Kernel reduction: scheduler, IPC, page-table management, interrupt routing only
- [ ] Capability-based security (SYS_CAP_GRANT / SYS_CAP_REVOKE)

### Phase 9: Hardware Drivers & Protocols (0.9.x)
- [ ] Full TCP/IP stack (ARP, IP, ICMP, UDP, TCP) with Ethernet NIC driver
- [ ] USB driver stack (UHCI/EHCI/xHCI)
- [ ] Hot-path secure call sequence layer (<seqguard.hpp>)
