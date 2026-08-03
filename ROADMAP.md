# Jarvis RTOS — Development Roadmap

**Build:** v0.3.7-dev | **Last Release:** v0.3.6

## Safety & Concurrency Guardrails (Strict)
- **Transition to Fine-Grained Locks:** All new synchronization code must use `SpinLock` + `SpinLockGuard` for short critical sections and `sync::Mutex` (without IrqGuard) for blocking paths. The global `IrqGuard` is deprecated for all uses except boot, panic, and test isolation.
- **Reference-Enforced Tasks:** When manipulating task blocks or IPC endpoints within the new init system or system calls, strictly enforce reference passing over raw pointers to prevent dangling lookups.
- **Zero-Allocation tmpfs Operations:** Ensure the initial `tmpfs` implementation relies on the pre-existing fixed `MemPool` / `BufferPool` infrastructure for its nodes to avoid unbounded allocations that violate resource tracking limits.

## Released — v0.3.6

### Syscall/VFS/ELF Boundary Audit (ASIL-D gate)
Source: `audits/boundary+syscall_audit.md` — verified kernel audit, 12 of 21
claims confirmed as real defects.  These harden the user/kernel trust boundary
in the syscall layer, VFS core, FAT32/tmpfs/devfs backends, and ELF loader.

**Pointer validation (CheckedPtr):**
- [x] **VULN-C1: `sys_fstat` raw Ring-3 pointer deref** — replace
      `reinterpret_cast<vfs::VfsStat *>(arg1)` with `checked()`; reject
      invalid user pointers with `-1`; mirror `sys_stat` pattern
      (src/kernel/syscall/syscall_handlers_fs.cpp).
- [x] **VULN-C2: `sys_ioctl` forwards unchecked Ring-3 pointer to driver** —
      minimum-bound `checked(arg2, sizeof(uint64_t))` at the syscall
      boundary; change `VnodeOps::ioctl` to take `CheckedPtr<void>`; update
      all `*_ioctl` stubs (devfs, fat32, pipe, procfs, tmpfs, initrd_fs).

**Authorization TOCTOU:**
- [x] **VULN-C4: authorize-then-resolve TOCTOU in path syscalls** — resolve
      first, capture `ino`+fs-instance in the stack-local `vfsd::Msg`,
      re-resolve after `vfsd_authorize` IPC and compare identity, then operate
      on the already-resolved vnode (sys_open/stat/mkdir/unlink/rmdir/chdir).

**Concurrency / refcount:**
- [x] **VULN-C5/C6: unsynchronized `Vnode::refcount` + `FdTable`** — make
      `refcount` a `std::atomic<int>`; use `fetch_add`/`fetch_sub` with the
      returned previous value for the zero-check in `FdTable::free`; guard the
      `sys_chdir` `cwd_vnode` swap with a per-task `cwd_lock_`.

**ELF loader / exec:**
- [x] **VULN-H1: uniform page permissions defeat W^X** — add a permission
      bitmask to `map_page_in_pml4`; derive per-segment W/X from `phdr->flags`;
      stack+heap mapped writable-only; set NX bit (bit 63) when not executable.
- [x] **VULN-H2: OOB ELF read via unchecked `phdr->offset+filesz`** — thread the
      real file size through `validate_segment`/`load_segments_and_stack`; add
      `offset+filesz > file_size` check after the existing overflow check.
- [x] **VULN-H4/W1: unbounded `validate_argv_envp` scan** — cap at
      `MAX_EXEC_ARGS`/`MAX_EXEC_ARG_LEN`; validate the full window with
      `checked()` before scanning; return combined argv+envp length out.
- [x] **VULN-U2: `setup_user_stack` unbounded underflow** — hard reservation
      check (`str_total + kStackReserve < mem::STACK_SIZE`) before pointer
      arithmetic; propagate failure through `load()`/`exec_into_current()`.

**Blocking / WCET:**
- [x] **VULN-W2: unbounded busy-wait in `tty_read`/`kbd_read`** — replace
      `UINT64_MAX` pause-spin with the `sys_receive` cooperative pattern
      (BLOCKED + `reschedule()`), or a wait/notify primitive posted from the
      IRQ handler if available.
- [x] **VULN-W3: `sys_receive` no bounded/timeout variant** — use the unused
      `arg3` slot as `timeout_ticks` (0 = block forever); add a deadline check
      to the blocking receive loop.

**Regression gate (PASSED 2026-08-02):** re-ran `syscall` (19/19), `process`
(43/43, incl. ELF), `vfs` (146/146), `security` (31/31) and the full `all`
suite (881/881) after each fix; 0 failures.  `make build` clean,
`check-style` Errors: 0.  Implementation spec:
`docs/v0.3.6-boundary-audit-spec.md`.

## Active Development — v0.3.7

### PfA Concurrency Redesign (Global/Race Variables)
Design spec: **`docs/v0.3.7-pfa-concurrency-design.md`** — replaces the flat
VAR-01..17 checklist. Applies PARAMETERISE FROM ABOVE (PfA) to the 17 "MAYBE"
variables from `docs/global-race-audit.md`, in two complementary directions:

- **PfA-A (eliminate globals):** config/test-only globals become fields of
  `SchedulerConfig` / `TestContext` injected down from `kernel_init`.
- **PfA-B (per-CPU context):** real shared state moves into `CpuContext`
  (threaded from above, Phase 8 SMP groundwork); remaining sharing uses
  atomics/seqlock with **one discipline per variable**.

- [ ] **PfA-A: `SchedulerConfig`** — `preempt_enabled_` (VAR-05),
      `sporadic_task_count_` (VAR-06), `suppress_terminated_log_` (VAR-07)
      → config fields passed to `Scheduler::init(cfg)`, read-only after.
- [ ] **PfA-A: `TestContext`** — `s_test_active_` (VAR-04),
      `g_test_deadline_monitor_pid` (VAR-15), `scheduler_dummy_save_rsp`
      (VAR-16) → injected struct; `nullptr` in production ⇒ flags false.
- [ ] **PfA-B: per-CPU debug state** — `s_wedge_emitted_`,
      `s_last_switch_tick_` (VAR-13), `s_lk0_count`, `s_last_holder`
      (VAR-14) → fold into `CpuContext::debug`.
- [ ] **PfA-B: `CpuContext::current`** — `current_task_ptr_` (VAR-01) per-CPU,
      atomic publish, RSP-ownership stays authoritative (INV-1).
- [ ] **PfA-B: `CpuContext::isr_nesting_depth`** — `isr_nesting_depth`
      (VAR-02) per-CPU (asm GS/TPIDR-relative); unify all C++ access to
      `__atomic_*`.
- [ ] **PfA-B: `Timer::ticks_`** — per-CPU atomic (VAR-09); `Timer::ticks()`
      accessor unchanged for 87 readers.
- [ ] **Single-owner/discipline:** `s_scan_requested_` (VAR-03) all-atomic;
      `s_deferred_kill_*` (VAR-08) under `scheduler_lock_`;
      `Keyboard` mods (VAR-10) byte-atomic; `MessageQueue::count` (VAR-11)
      relaxed-atomic for unlocked readers; `BufferPool` cookie/page-count
      (VAR-12) atomic.
- [ ] **Deferred to Phase 8:** `hhdm_modified_` (VAR-17) re-audit under SMP.
- [ ] Delete remediated variables from `docs/global-race-audit.md`; regression
      gate: `scheduler`, `ipc`, `sporadic`, `ipc_blocking` 0-failure.

### Disabled test groups (pre-existing, incompatible with snapshot isolation)
| Group | Tests | Reason |
|-------|-------|--------|
| `pml4_clone` | 0 | Re-enabled — all 6 tests pass (all-1 480–485); HHDM PD save/restore landed |
| `vmm_hhdm` | 0 | Fixed by HHDM PD save/restore (#1) — tests re-enabled |
| `virtio` | 0 | Already works — boot probe allocates PT pages in pool baseline |
| `dma` | 0 | Already works — allocates within 0-128MB, HHDM restore handles cleanup |
| `microkernel_transition` | 1 | KernelApiPureFunctions memcpy stack corruption (~657) |
| **Total disabled** | **1** | |

### Stack Guard & Fork (Deferred)
- [ ] Stack guard page via private VA window (requires snapshot-safe page table pool)
- [ ] `page_table_shared_` removal — complete deep-copy fork (walk all user entries, allocate new PDPT/PD/PT, copy contents). Current state: config + pool done.

## Active Development — v0.3.9

### H2 Deferred-Switch Race Fix (debug `all` hang with trace OFF)

Source: `docs/ipc_blocking-analysis.md` §H2 — the split-phase deferred context
switch publishes `scheduler_load_rsp_from` / `scheduler_load_cr3_from` /
`scheduler_save_rsp_to` as separate stores; a timer ISR applying the pair can
save the harness's live RSP (boot stack, kernel-image space) into the wrong
TCB when `current_task_ptr_` has drifted.  With `CONFIG_DEBUG_IPC_SCHED`
**off** the race is deterministic: debug `all` hangs 2/2 at
`ipc_send_sync_roundtrip` (~test 77/78).  With the trace **on** the extra
serial latency masks it (881/881 verified 2026-08-01).  The debug `all`
development gate keeps the trace ON until this is fixed.

- [ ] **Root cause (confirmed):** `switch_to_task` owner-resolution
      (scheduler.cpp ~1664-1701) scans TCBs for the live-RSP owner and finds
      **none** when the harness runs on the boot stack (not a TCB stack), so
      `save_target` stays `&TASK_STACK_PTR(current)` and the ISR saves a
      boot-stack RSP into the harness TCB.  `scheduler_diag_pre_save()`
      (scheduler.cpp ~2480) catches it as `cur_rsp` outside
      `kstack=[...] owners: (empty)`.
- [ ] **Fix candidates (from analysis doc §Next steps):**
      (1) make the deferred-switch pair atomic — publish RSP+CR3 under a
      single generation so the ISR never applies a half-written pair
      (isr_stubs.asm:106-171); (2) treat a boot-stack harness RSP as valid
      (no-owner ⇒ save into the physically-running harness, or skip the
      save entirely for the harness/idle path); (3) fix the
      `current_task_ptr_`/runq desync (INV-2) that leaves a live task out of
      the runq and not `current`.
- [ ] **Verification:** debug `all` must pass 881/881 with the trace **off**;
      then re-verify `release all` (84/84) and `check-style` Errors: 0.

## Past Releases

See `ROADMAP_done.md` for completed items in released versions (v0.2.x — v0.3.6).

---

## Future Roadmap (Aspirational)

### Phase 4.5: Memory Protection (0.4.x) — prerequisite for safe SMP
- [ ] **Requirement spec:** `docs/memory-protection-spec.md` (REQ-MP-01..06). Current state: user↔user isolation + user-stack guard pages present; kernel-task↔kernel-task isolation ABSENT; software canaries absent. Decisions: full private kernel page tables, both MMU guard pages + software canaries, HW enforcement (SMAP/SMEP/PAN/PXN) recommended-not-mandatory.
- [ ] **0.4.0-MP1** — Private kernel-half page tables per kernel task (clone kernel PML4, private data/bss/stack frames, CR3 switch; preserve HHDM for kernel→user access)
- [ ] **0.4.0-MP2** — MMU red-zone guard pages between text/data/heap/stack segments (kernel + user tasks)
- [ ] **0.4.0-MP3** — Software sentinel canaries at segment boundaries, verified on syscall + context-switch entry
- [ ] **0.4.0-MP4** — Optional HW enforcement: SMAP/SMEP (x86_64) / PAN/PXN (aarch64) with `stac/clac` audit
- [ ] **0.4.0-MP5** — Verification suite: cross-task #PF tests, canary-tamper detection, HHDM kernel→user read, SMAP/PAN negatives

### Phase 5: SMP + Multicore (0.4.x)
#### 0.4.1–0.4.2 — APIC & SMP Boot
- [ ] Local/IO APIC, X2APIC, per-CPU GDT/TSS, INIT-SIPI AP startup
- [ ] TPR-based interrupt prioritization, core state isolation

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
