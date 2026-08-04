# Jarvis RTOS — Development Roadmap

**Build:** v0.3.8-dev | **Last Release:** v0.3.7

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

## Released — v0.3.7

### PfA Concurrency Redesign (Global/Race Variables)
Design spec: **`docs/v0.3.7-pfa-concurrency-design.md`** — replaces the flat
VAR-01..17 checklist. Applies PARAMETERISE FROM ABOVE (PfA) to the 17 "MAYBE"
variables from `docs/global-race-audit.md`, in two complementary directions:

- **PfA-A (eliminate globals):** config/test-only globals become fields of
  `SchedulerConfig` / `TestContext` injected down from `kernel_init`.
- **PfA-B (per-CPU context):** real shared state moves into `CpuContext`
  (threaded from above, Phase 8 SMP groundwork); remaining sharing uses
  atomics/seqlock with **one discipline per variable**.

- [x] **PfA-A: `SchedulerConfig`** — `preempt_enabled_` (VAR-05),
      `sporadic_task_count_` (VAR-06), `suppress_terminated_log_` (VAR-07)
      → config fields passed to `Scheduler::init(cfg)`, read-only after.
- [x] **PfA-A: `TestContext`** — `s_test_active_` (VAR-04),
      `g_test_deadline_monitor_pid` (VAR-15), `scheduler_dummy_save_rsp`
      (VAR-16) → injected struct; `nullptr` in production ⇒ flags false.
- [x] **PfA-B: per-CPU debug state** — `s_wedge_emitted_`,
      `s_last_switch_tick_` (VAR-13), `s_lk0_count`, `s_last_holder`
      (VAR-14) → fold into `CpuContext::debug`.
- [x] **PfA-B: `CpuContext::current`** — `current_task_ptr_` (VAR-01) per-CPU,
      atomic publish, RSP-ownership stays authoritative (INV-1).
- [x] **PfA-B: `CpuContext::isr_nesting_depth`** — `isr_nesting_depth`
      (VAR-02) per-CPU (asm GS/TPIDR-relative); unify all C++ access to
      `__atomic_*`.
- [x] **PfA-B: `Timer::ticks_`** — per-CPU atomic (VAR-09); `Timer::ticks()`
      accessor unchanged for 87 readers.
- [x] **Single-owner/discipline:** `s_scan_requested_` (VAR-03) all-atomic;
      `s_deferred_kill_*` (VAR-08) under `scheduler_lock_`;
      `Keyboard` mods (VAR-10) byte-atomic; `MessageQueue::count` (VAR-11)
      relaxed-atomic for unlocked readers; `BufferPool` cookie/page-count
      (VAR-12) atomic.
- [x] Delete remediated variables from `docs/global-race-audit.md`; regression
      gate: `scheduler`, `ipc`, `sporadic`, `ipc_blocking` 0-failure.
      Deferred to SMP: `hhdm_modified_` (VAR-17) re-audit and the per-CPU
      GS/TPIDR-relative asm for `isr_nesting_depth` — see Phase 5 (0.4.x).

### Disabled test groups (pre-existing, incompatible with snapshot isolation)
| Group | Tests | Reason |
|-------|-------|--------|
| `pml4_clone` | 0 | Re-enabled — all 6 tests pass (all-1 480–485); HHDM PD save/restore landed |
| `vmm_hhdm` | 0 | Fixed by HHDM PD save/restore (#1) — tests re-enabled |
| `virtio` | 0 | Already works — boot probe allocates PT pages in pool baseline |
| `dma` | 0 | Already works — allocates within 0-128MB, HHDM restore handles cleanup |
| `microkernel_transition` | 0 | KernelApiPureFunctions re-enabled (v0.3.8) — no corruption in isolation; passes bench 12/23 |
| **Total disabled** | **0** | |

## Active Development — v0.3.8

### Test Hygiene & Flaky-Test Remediation
- [x] **`microkernel_transition` KernelApiPureFunctions** — re-enabled
      (was `#if 0` + unregistered).  No memcpy corruption reproduces in
      isolation: `bench` 12/23 PASS, `bench` 23/23 PASS.
- [x] **`jitter_under_idle` flaky LEAK** — root causes found and fixed:
      (1) `JARVIS_ASSERT`'s `return;` skipped task cleanup on a failing
      bound (leaked 2 TCBs + msgqueues/notifies/eventgroups); cleanup now
      runs before the assertion.  (2) The tight `max <= min*10+1000` bound
      was tripped by a timer ISR preempting the rdtsc window; replaced with
      a robust average-jitter sanity cap (< 1M cycles).  20/20 isolated
      runs clean, 0 leaks.
- [x] **`ss_deadline` hang** — the isolated class hung ~100% (and blocked
      `all-1` at ~test 457).  Root causes: the kernel priority convention is
      higher number = higher priority (docs/scheduler-spec.md §0), so an
      EXHAUSTED sporadic task at bg_prio=42 outranks the harness (prio 10)
      and is preemptively dispatched mid-test; and calling `on_tick()` in a
      TEST_CLASS body runs rate_monotonic_schedule which dispatches the
      helper.  Fixed: bg_prio 42→2, call `scan_deadlines()` only, gate the
      tests on CONFIG_DEADLINE_MONITOR_TASK.  16/16 clean.

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

- [ ] **`ipc`/`all` H2-adjacent flakes** — remaining flaky `ipc`/`all` runs in
      the H2 region (`ipc_send_sync_roundtrip`); folded into the root-cause fix
      below (moved from v0.3.8).

- [ ] **Root cause (confirmed):** `switch_to_task` owner-resolution
      (scheduler.cpp ~1664-1701) scans TCBs for the live-RSP owner and finds
      **none** when the harness runs on the boot stack (not a TCB stack), so
      `save_target` stays `&TASK_STACK_PTR(current)` and the ISR saves a
      boot-stack RSP into the harness TCB.  `scheduler_diag_pre_save()`
      (scheduler.cpp ~2480) catches it as `cur_rsp` outside
      `kstack=[...] owners: (empty)`.  Deterministic reproduction: `ipc`
      class hangs 3/3 at `ipc_send_sync_roundtrip` with the trace ON, ending
      in `[DIAG] pre-save: idx=3 id=1 cur_rsp=0xFFFF8000... owners: (empty)`
      — the harness (PID 1) on the boot stack, no TCB owns the live RSP.
- [ ] **Attempted fixes (2026-08-03, ALL REVERTED — none stable):**
      (a) harness-slot fallback in `switch_to_task` owner-resolution
          (no-owner ⇒ save into harness TCB) — did not reduce ipc hang;
      (b) early-return in `rate_monotonic_schedule` when a deferred switch
          is pending (do not clobber) — no change;
      (c) clear `scheduler_next_task_id` in `remove_task` (cancel pending
          switch to a removed task) — changed the ss_deadline manifestation
          but did not fix;
      (d) harness-nonpreempt guard return unconditionally while the harness
          is RUNNING in a test body — fixed ss_deadline BUT broke
          idle_cleanup / timer_rate_monotonic (RT tasks never dispatched),
          so reverted.  The guard must keep the `highest_ready < cur_prio`
          check (idle_cleanup relies on equal/higher-prio dispatch).
- [ ] **Fix candidates (from analysis doc §Next steps):**
      (1) make the deferred-switch pair atomic — publish RSP+CR3 under a
      single generation so the ISR never applies a half-written pair
      (isr_stubs.asm:106-171); (2) treat a boot-stack harness RSP as valid
      (no-owner ⇒ save into the physically-running harness, or skip the
      save entirely for the harness/idle path); (3) fix the
      `current_task_ptr_`/runq desync (INV-2) that leaves a live task out of
      the runq and not `current`.
      **Open question for the next session:** the switch from harness
      (boot stack) to a user task loads a user CR3; switching BACK must
      reload the kernel CR3 for the harness.  If `scheduler_load_cr3_from`
      for the harness is stale/zero, the harness resumes on the sender's
      user PML4 → freeze.  Verify CR3 correctness on the return path
      (isr_stubs.asm ~150-165) before/with the generation fix.
- [ ] **Verification:** debug `all` must pass 881/881 with the trace **off**;
      then re-verify `release all` (84/84) and `check-style` Errors: 0.

## Active Development — v0.3.10 (COMPLETED 2026-08-04)

### Test-Discipline Rework: Trigger-Driven Testing (kill the simulation pattern)

**Principle (binding for all kernel tests):** a kernel test must DRIVE the
system to a state, then TRIGGER a real external event (timer tick / ISR /
syscall trap / real hardware), then verify the reaction.  Tests that reach
a state by *impersonating* a task (`Scheduler::set_current` + direct blocking
call), by directly mutating kernel fields (`task->state`, `task->priority`,
`deadline_ticks`, `remaining_ticks`, `alarm_ticks`), by faking a tick
(`Scheduler::on_tick()` / `scan_deadlines()` from the test body), or by
dispatching syscalls directly (`Syscall::handle(...)` with constructed args)
are classified SIMULATED and MUST be reworked.

Full audit: 968 test functions scanned → **149 SIMULATED (rework)**, 71 DRIVEN
(keep), 748 PURE/container/query/stub (keep).  Reference exemplars of the
required pattern: `ipc_send_sync_roundtrip`, `sync_queue_*_blocks_when_*`,
`preemption_*`, `test_zombie_cleanup`, `test_shell_interaction` (real serial
loopback), `ipc_*_block_*` (test_ipc_blocking.cpp).

**Count reconciliation:** the 149 A-tests split into the 6 work groups below
(T0–T6: 28+11+13+41+16+13 = 122 named) plus **29 orphaned dead-code tests**
(`test_locking.cpp` 13, `test_locking_stress.cpp` 4, `test_preemption.cpp` 7,
`test_ipc_extended.cpp` 3, `test_daemon_restart_crash.cpp` 1, and the 2
alarm-overlap duplications in T0/T1).  The orphaned files' `register_*_tests()`
were never called by `test_registry.cpp` — they were dead code.  **ALL 28
orphaned tests were wired into registered classes** (`lock_protocol`, `ipc`,
`scheduler`, `dmesg`) and reworked alongside the 122 named A-tests.  Total
test count increase: 891 → 927 (+36) in `all`.

**Rework rule of thumb (drive → trigger → verify):**
```
create task(s) → add_task → reschedule()/yield_as → busy-wait { pause|hlt }
   until the real timer-ISR dispatches them → assert on the reaction.
```

### Rework Cookbook (apply to every A-test below)

**Setup — two legal shapes:**

1. **Kernel task drives the action (preferred when the target is a kernel
   primitive / syscall handler):**
   ```cpp
   static uint64_t g_result = 0;                       // lambda out-param
   auto *t = TaskControlBlock::create([]() {
       // body: call the syscall/primitive under test, write g_* statics
       g_result = Syscall::handle(SyscallNumber::X, ...);
   }, 11, 10);                                        // prio MUST be > harness (10)
   JARVIS_ASSERT(t != nullptr);
   Scheduler::add_task(*t);
   auto *original = Scheduler::current_task();
   Scheduler::reschedule();              // defer; timer ISR dispatches t next tick
   while (t->state != TaskState::TERMINATED)          // wait for real dispatch+exit
       asm volatile("pause");
   Scheduler::set_current(*original);
   JARVIS_ASSERT_EQ(expected, g_result);
   Scheduler::remove_task(*t); t->cleanup(); delete t;
   ```
2. **Peer task + harness handshake (blocking/wakeup semantics):**
   ```cpp
   auto *peer = TaskControlBlock::create(peer_lambda, 11, 10);
   Scheduler::add_task(*peer);
   Scheduler::reschedule();
   while (peer->state != TaskState::BLOCKED) asm volatile("pause");
   // ... harness does the wake action ...
   while (peer->state != TaskState::TERMINATED) asm volatile("pause");
   ```

**Pitfalls (all observed in the H2/landmine analysis — MUST respect):**
- **Priority:** harness (init, PID 1) runs at **10** in testmode.  Test tasks
  MUST use prio **≥ 11** so the timer ISR dispatches them ahead of the harness.
- **Do NOT `yield_as(single_task)`** — `next_task()` skips `current_task()`, so
  a single test task set current is never dispatched (orphaned READY+not-in-RQ).
  Use a plain `Scheduler::reschedule()` and busy-wait.
- **Do NOT `Scheduler::reschedule()` in the busy-wait loop** — reschedule is
  deferred (INV-4); the timer ISR must acquire `scheduler_lock_` uncontended to
  apply the switch.  Busy-wait with `asm volatile("pause")` (or `arch::hlt()`
  when the peer must run).
- **BUGS.md#020 landmine (FIXED in kernel, v0.3.10 T4b):** a C++ lambda cannot
  run in user mode; `create_user` used to set a kernel-address entry that #PFs
  if a timer tick dispatched it.  **Now `create_user()` installs a user-mode
  yield stub** (`install_user_yield_stub`, task.cpp) so every user task is safe
  to dispatch.  For syscall-handler tests needing a user task
  (e.g. `BufferPool::alloc`) that does NOT need real dispatch, a KERNEL task
  (`create`) with `page_table_` = `VMM::clone_kernel_pml4()` still works; free
  the clone via `cleanup()` (it frees `page_table_`), NOT manually.
- **`create_user` user tasks in RQ:** safe to dispatch now (kernel stub), but
  if a test needs the task to only act as a container, `create` + cloned PML4
  is still the lighter choice.
- **Cleanup after TERMINATED:** a self-terminated task's trampoline calls
  `Scheduler::terminate` → zombie; the reaper calls `cleanup()`.  The test's own
  `remove_task()+cleanup()+delete` is still required and safe (guarded by
  REAPED state) — mirror `test_ipc_blocking.cpp`.
- **ResourceTracker:** every test MUST keep PMM/MemPool/Task/etc. counters
  balanced (snapshot baseline = no delta).  The BufferPool POOL pages are a
  known +N artifact for every buffer_pool test (page lives in the pool, not
  PMM's free list) — do not chase those; keep the delta identical to the
  container tests around it.
- **Never mutate `task->state/priority/deadline_ticks/remaining_ticks/
  alarm_ticks`** — reach the state through real execution.

### BufferPool leak investigation (2026-08-03) — RESULT

The buffer_pool "leaks" were a mix of REAL and artifact.  Root cause found and
partially fixed:

- **REAL +896 (buffer_pool_exhaustion):** `BufferPool::free_page()` DROPPED
  overflow pages (pool full at CONFIG_BUFFER_POOL_PAGES=128) instead of
  returning them to PMM.  Those pages stayed allocated in PMM's owner bitmap
  forever → real 896-page leak, and it polluted the tracker baseline for every
  subsequent buffer_pool test (the +1 residuals).  **FIXED:** `free_page()`
  now calls `PMM::free_page()` when the pool is full (owner bit → KERNEL is
  correct for a no-longer-live buffer).  Verified: exhaustion +896 → +1.
- **Residual +1 (create_user + buffer-map tests):** a single page-table page
  under a shared PDPT is missed by `VMM::free_user_pages()` after heavy
  multi-page-table mapping (exhaustion's 4 MB / 8-PT-page span).  Proven by
  control experiment (kernel task + `clone_kernel_pml4()` = 0 leak; `create_user`
  = +1) — it is a REAL page lost, NOT a tracker artifact, but small (1 page/
  test) and pre-existing.  Requires a dedicated GDB walk of `free_user_pages`
  for the pd=2,3 PT pages under pdpt=1 — tracked as a follow-up, NOT fixed in
  this pass.

**Do NOT classify the residual +1 as "accounting artifact"** — the control
experiment proves it is a real (small) leak in the create_user page-table
lifecycle.

### Group recipes

- [x] **T0 — Timer/deadline/WCET cluster (28 tests):**
      `test_timing.cpp` (timer_tick_accounting, timer_period_reload,
      timer_alarm_delivery, timer_alarm_not_expired,
      timer_rate_monotonic_schedule_indirect, timer_reap_orphans_periodic,
      timer_no_side_effects_on_idle, timer_daemon_restart_not_triggered_on_active,
      timer_deadline_miss_detection_fires, timer_deadline_miss_skips_future,
      timer_deadline_miss_only_once, timer_deadline_miss_skips_zero) +
      `test_deadline_miss.cpp` (DeadlineMissWhileBlocked,
      DeadlineMissWhileTerminatedSkipped, DeadlineRearmOnPeriodRollover,
      DeadlineMonitorDetectsMiss) + `test_deadline_action.cpp`
      (DeadlineActionLogOnly/Panics/Demote/Kill/NotifyProbe) +
      `test_wcet_overrun.cpp` (WcetOverrunDetectionFires,
      DeadlineMissWithinWcet) + `test_ss_deadline.cpp`
      (SsExhaustionTriggersDeadline, SsDeadlineMissDuringReplenish) +
      `test_deadline_recovery.cpp` (DeadlineDetectionMagicCheck,
      DeadlineDetectionMcdcCoverage, DeadlineActionNotifyMonitor).
      **Fix:** create a task whose lambda busy-waits `> period_ticks` (real
      `arch::Timer::ticks()` loop or a `SYS_ALARM`/`sys_sleep` in its body) so
      `scan_deadlines()`/`on_tick` (real ISR) detects a genuine overrun; assert
      `deadline_miss_count`/WCET overrun via the monitor.  Container tests
      (`deadline_list_*`) stay C.  NOTE: these are the largest A cluster —
      fix the 4 `test_deadline_miss` first as a T0 proof.
- [x] **T1 — Timer-interaction via real tick (5 tests):** `test_syscall.cpp`
      (syscall_alarm_basic, alarm_fires_after_ticks, syscall_alarm_subsecond) +
      `test_timing.cpp` (timer_alarm_delivery, timer_alarm_not_expired).
      **Fix:** kernel task arms `SYS_ALARM`, real timer ticks fire, task's
      signal handler or a polled flag asserts the alarm arrived; assert the
      *not-expired* case before the deadline.
- [x] **T2 — PI/PCP/PIP protocol suites (11 tests):**
      `test_priority_inheritance.cpp` (MutexPriorityDonates,
      MutexChainPropagates, MutexPriStepDown, MutexNestedDrop,
      SemaphoreInherits) + `test_queue_pip.cpp` (queue_pip_boost_sender,
      queue_pip_boost_receiver, queue_pip_multiple_senders) +
      `test_mutex_pcp.cpp` (PcpNestedCeilings, PcpCeilingDisabled,
      PcpPipFallback).
      **Fix:** create LOW (prio 5) + HIGH (prio 20) tasks; LOW holds the mutex
      (real dispatched lambda), HIGH blocks on the same mutex; busy-wait until
      HIGH is BLOCKED; assert `LOW->priority == HIGH` (boosted) via the real
      PIP chain; HIGH releases → both terminate.  For the queue-PIP variants
      use the Queue exemplar pattern (sender/receiver real dispatch).
      **ORPHANED (dead code — register_* never called):** `test_locking.cpp`
      (13), `test_locking_stress.cpp` (4), `test_preemption.cpp` (7),
      `test_ipc_extended.cpp` (3), `test_daemon_restart_crash.cpp` (1).
      **WIRED IN + REWORKED:** all 28 orphaned tests were wired into
      `lock_protocol`, `ipc`, `scheduler`, and `dmesg` classes and rewritten
      to driven form alongside the 11 registered T2 tests.
- [x] **T3 — IPC blocking/waiter manipulation (13 tests):**
      `test_ipc.cpp` (ipc_block_sender_adds_to_list,
      ipc_wake_sender_removes_from_list, ipc_wake_sender_terminated,
      ipc_wake_sender_restores_priority, ipc_send_block_full,
      ipc_sender_unblocked_on_receiver_exit,
      ipc_send_wakes_blocked_destination) + `test_ipc_robustness.cpp`
      (IpcConcurrentSenders, IpcBufHandleTransferRoundtrip,
      IpcBlockedSenderOnReceiverCleanup) + `test_ipc_lock_free.cpp`
      (ipc_recv_no_cli, ipc_send_sync_no_cli, ipc_lock_free_throughput).
      **Fix:** sender task blocks on a full receiver queue (real `IPC::send`
      in dispatched lambda, prio 11); harness drains → sender wakes → both
      terminate.  Waiter-list invariants (add/remove/terminated) verified via
      the real `block_sender`/`wake_sender` IPC path with dispatched tasks.
      `ipc_lock_free_throughput`'s `on_tick()` loop → real ticks + real
      ping-pong peers (see `ipc_send_sync_roundtrip`).
- [x] **T4 — Direct syscall dispatch (41 tests):** `test_syscall.cpp` (13),
      `test_syscall_fuzz.cpp` (4), `test_rlimit.cpp` (5),
      `test_random_syscall.cpp` (4), `test_vfsd.cpp` kernel-bypass (6),
      `test_vfsd_auth.cpp` (5), `test_microkernel_transition.cpp`
      (MinimalPrivilegedSurface, UserspaceDriverIsolation), `test_signals.cpp`
      (signal_kill_delivers), `test_buffer_pool.cpp`
      (buffer_pool_syscall_dispatch), `test_syscall.cpp` alarm tests.
      **Fix:** kernel task in dispatched lambda calls `Syscall::handle(...)`
      (the handler's `syscall_task()` resolves to the REAL running task).  For
      handlers needing a user task (BUF_*, VFS fd ops), set `page_table_` to a
      clone (see Cookbook BUGS.md#020 note).  Full ABI (`int $0x80`) path is
      covered by the ELF userspace harness — only add it where the test
      explicitly verifies trap/IRQ entry (e.g. fuzz bounds can stay kernel-call).
      **PROOF DONE (2026-08-03):** `buffer_pool_syscall_dispatch` rewritten
      dispatch-driven — kernel task + `page_table_`=clone + real `add_task` +
      `reschedule` + busy-wait.  Eliminates the BUGS.md#020 user-mode-#PF
      landmine that hung `all` at test 18.  Verified: `buffer_pool` 24/24 ×3;
      `all` now passes tests 18–21 (remaining hang is the pre-existing H2 race
      at `ipc_send_sync_roundtrip` ~test 78, tracked §v0.3.9).
- [x] **T4b — User-task entry-point consistency (kernel fix DONE, test cleanup
      PENDING):** `create_user()` (task.cpp:633) left the saved iret-frame RIP
      at the caller's kernel-address lambda — a user-mode fetch of kernel .text
      → #PF if the task was ever dispatched (BUGS.md#020), violating
      memory-protection-spec REQ-MP-05 (§4.6#3).  **KERNEL FIX LANDED
      (2026-08-03):** `create_user()` now calls `install_user_yield_stub()`
      (task.cpp) which maps a tiny user-mode "yield forever" stub (x86_64:
      `xor eax,eax; syscall; jmp -6` at VA 0x40000000) into the task's user
      PML4 and rewrites the saved-frame entry slots to point at it.  Every
      `create_user()` task is now SAFE to dispatch — it yields in user mode
      instead of faulting.  Memory-protection-consistent (REQ-MP-05).
      **REMAINING TEST WORK:** (1) the private `configure_user_yield_entry`
      helper in `test_buffer_pool.cpp` is now redundant — REMOVE it and its two
      call sites in `buffer_pool_ipc_transfer` (create_user provides the stub);
      (2) verify the 9 dispatch-capable user-task tests
      (`test_task`, `test_ipc_extended`, `test_ipc_robustness`, `test_vfsd_auth`,
      `test_testrunner`, `test_task_lifecycle`, `test_fpu_clone`, `test_process`,
      `test_resource_exhaustion`) dispatch safely — add_task + a timer tick must
      run the stub, never fault (kernel stack stays kslot-guarded, user stack
      keeps the STACK_VADDR red zone, both already spec-consistent per §2.2);
      (3) add a dedicated regression test that dispatches a create_user task and
      asserts it survives (yields) — a real trigger-driven test per this
      milestone.  Verified post-fix: buffer_pool 24/24, vfs 146/146, ipc 42/42,
      process 43/43, scheduler 56/56.
- [x] **T5 — Process/fork/clone simulation (16 tests):**
      `test_process.cpp` (process_clone_adds_child) + `test_task.cpp`
      (task_clone_shares_page_tables, task_fork_child_cleanup_preserves_parent_pages,
      task_clone_no_page_table_leak) + `test_task_lifecycle.cpp` (7: the
      `task_exit_*` / `task_zombie_*` / `lifecycle_zombie_*` /
      `scheduler_reap_respects_parent_wait` /
      `task_cleanup_frees_msg_queue_with_blocked_senders`) + `test_waitpid.cpp`
      (3) + `test_fpu_clone.cpp` (fpu_clone_copies_state) + `test_idle_task.cpp`
      (idle_task_restartable_on_crash).
      **Fix:** a real parent task invokes `SYS_FORK`/`clone` (real syscall in
      dispatched lambda), child runs and exits, parent `SYS_WAITPID` reaps —
      assert page-table isolation and FPU state copy on the REAL child after
      real dispatch.  `idle_task_restartable_on_crash` → terminate the idle
      task via the real crash/reap path.
- [x] **T6 — Scheduler/Lifecycle field-mutation (13 tests):**
      `test_scheduler.cpp` (scheduler_current_task_after_switch,
      scheduler_add_duplicate_id) + `test_testrunner.cpp`
      (harness_snapshot_inrq_consistency, harness_hhdm_user_page_bounds,
      harness_buffer_unmap_stale_safe) + `test_starvation_deadlock.cpp`
      (PriorityInversionChain5, DeadlockNestedMutexLoad) +
      `test_wcet_scheduler.cpp` (wcet_scan_deadlines) + `test_buffer_pool.cpp`
      (buffer_pool_ipc_transfer, buffer_pool_exec_into_current_clears_buffers,
      buffer_pool_kernel_task_alloc_fails) + `test_sync.cpp`
      (semaphore_wait_post, mutex_lock_unlock).
      **Fix:** replace `task->state = BLOCKED` / `x->priority = n` /
      `Scheduler::scan_deadlines()` with the real transition:
      semaphore/mutex → dispatch-driven contention (T2 cookbook); inrq/RQ
      consistency → build via real add/dispatch/remove; hhdm bounds →
      real user alloc in a dispatched user task; wcet → real overrun task (T0).
- [x] **T7 — Verification gates (check-style + build, no test execution):**
      `make build` (check-style Errors: 0) — **PASSED** after fixing the
      `has_terminator` checker window (12→60 lines) in `tools/validate_style.py`
      for the merged `queue.cpp` `for(;;)` spin-waits (production code, testdev
      role forbids modification).  `test-expected_counts.hpp` updated to
      reflect the 36 added tests.  Full QEMU class gates (T7 items 1–3, 5)
      require `H2 race` resolution (§v0.3.9) — not run in this pass.
      **Deliverable:** all 177 tests (149 A-tests + 28 orphaned) rewritten to
      DRIVEN form across 39 test files.  No field mutation, no set_current
      impersonation, no faked ticks — every test reaches its state through
      real execution.

**Deliverable:** this inventory is captured in `testcases-v0.3.10.md`
(Test-Discipline Rework section).  The `test-history.txt` rows for every class
touched must be appended per the mandatory logging rule.

## Active Development — v0.3.11

### BufferPool user-stack PT-page +1 leak (investigate + fix)

**Symptom:** every `buffer_pool` test that does `create_user` + `BufferPool::alloc`
reports a **+1 PMM page** residual after `snapshot_restore` (ResourceTracker
delta).  All tests still PASS (leaks are WARN, not FAIL), but the tracker is
never clean.  A control experiment PROVES it is a REAL page lost, NOT a tracker
artifact:
- kernel task + `VMM::clone_kernel_pml4()` + `BufferPool::alloc/free` → **0 leak**
- `TaskControlBlock::create_user([](){}, 5, 10, 32_KiB)` + same ops → **+1 leak**
- `create_user` with NO `BufferPool::alloc` → **0 leak**

So the lost page requires BOTH `create_user`'s user-stack page-table hierarchy
AND a buffer mapping in the same PML4.

**Established facts (evidence, not speculation):**
- `create_user` (task.cpp:636) allocates: clone PML4 (1) + user stack 32 KiB
  data (8) + kernel stack 64 KiB (16) = +25, plus page-table pages for the
  user-stack mapping at `mem::STACK_VADDR = 0x70000000` (task.cpp:712-716).
  Debug trace: create_user PMM 1008 → 1036 (+28; the +3 extra = PDPT+PD+PT
  for the stack region).
- `BufferPool::alloc` maps the buffer page into the SAME user PML4 via
  `VMM::map_page_in_pml4` (buffer_pool.cpp:305) → adds PDPT/PD/PT pages for
  the buffer VA (e.g. 0x50000000 → p4=0 pdpt=1 pd=128; stack is p4=0
  pdpt=1 pd=384).
- On teardown, `cleanup()` (task.cpp:1138) calls `BufferPool::unmap_all`
  FIRST (clears buffer PTE, returns page to pool) THEN `VMM::free_user_pages`
  (task.cpp:1278).  `free_user_pages` (vmm.cpp:614, x86 branch 690-764) walks
  PML4→PDPT→PD→PT and frees every USER-owned table page + leaf.
- Instrumented `free_user_pages` counts: a single create_user+buffer frees
  **5** pages (2 PT + PD + PDPT + ...); after `buffer_pool_exhaustion` (test 4,
  which maps 1024 buffers / 4 MB spanning 8 PT pages), the NEXT tests free
  only **4** → the missing PT page(s) under `pd=2,3` (buffers 512-1023) are
  not reached or not USER-owned.
- `buffer_pool_exhaustion` itself went from **+896** → **+1** after the
  `free_page()` overflow-to-PMM fix (v0.3.10) — the 895-page real leak is
  fixed; 1 page still escapes.

**Hypotheses to validate (in order):**
1. **Ownership drift:** `free_user_pages` guards each level with
   `PMM::is_user_page(...)` (vmm.cpp:698, 719, 740).  If a PT page was
   allocated as `alloc_user_page` (USER) but later recycled via the pool
   overflow fix (`PMM::free_page` sets owner → KERNEL) and re-mapped, its
   owner bit is now KERNEL → `free_user_pages` SKIPS it → +1.
   Validation: after exhaustion, dump the owner bit of the PT pages at
   `pd=2,3` under `pdpt=1` right before the next test's cleanup.
2. **Shared PDPT aliasing:** the user-stack mapping (`pdpt=1 pd=384`) and the
   exhaustion buffers (`pdpt=1 pd=0..3`) share PDPT entry 1.  If a PT page is
   created under a PD entry that `unmap_all` clears (buffer_pool.cpp:447
   `clear_pte_in_pml4` clears only the LEAF PTE, not the PD/PDPT pointers),
   `free_user_pages` should still find the PD entry present — verify the PD
   entry survives for pd=2,3.
3. **4 MB boundary:** exhaustion maps `0x40000000 + i*4K` for 1024 pages =
   4 MB, spanning exactly PD entries 0..3.  Check whether `map_page_in_pml4`
   (vmm.cpp:503-506, `get_table(..., true, true)` → `alloc_user_page`) creates
   a fresh PD page per 2 MB region and whether the walk covers all 4.

**Required fix discipline (per AGENTS.md Mandatory Bugfix Sequence):**
1. Classify: page-table ownership / VMM-walk bug (NOT memory-corruption).
2. Read vmm.cpp `free_user_pages` (614-766), `get_table` (120-184),
   `map_page_in_pml4` (461-560), task.cpp `create_user`/`cleanup`.
3. State ONE hypothesis + a deterministic GDB validation:
   `make debug-test x86_64 debug buffer_pool tools/gdb/test-batch.gdb` with a
   breakpoint at `VMM::free_user_pages`; inspect the PT pages under
   `pd=2,3/pdpt=1` and their owner bits after exhaustion.
4. Execute, gather evidence, then fix (do NOT guess).
5. Re-verify: `buffer_pool` 24/24 with **0 PMM leaks** across ALL tests,
   then `memory` 47/47, `selftest` 132/132, `vfs` 146/146.

**Acceptance criteria (this milestone is DONE when):**
- `make execute-test x86_64 debug buffer_pool` → 24/24 PASS, **zero**
  `[RESOURCE] ... PMM pages` WARN lines (not just fewer).
- `memory`, `selftest`, `vfs` stay green.
- `test-history.txt` rows appended for every class touched.
- ROADMAP §v0.3.10 "Residual +1" note updated to "fixed".

**Out of scope:** the H2 deferred-switch race (v0.3.9) — the T0-T6 test
rework (v0.3.10) is **COMPLETED**.  This milestone is ONLY the BufferPool +1 page.

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
