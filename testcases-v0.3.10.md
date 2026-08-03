# Test Cases — v0.3.10 (Phase 4: Documentation & Certification Artifacts)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### WCET Analysis Report — test_wcet_report.cpp
- `docs/wcet_analysis.md` generated with measured max cycles per kernel function
- Toolchain: `objdump -d` + static analysis (aiT, OTAWA, or custom script)
- WCET report covers: scheduler dispatch, IPC send/recv, syscall entry/exit, IRQ entry/exit, MemPool alloc/free
- Each WCET figure includes test environment (QEMU or hardware, CPU model, clock speed)
- WCET figures traceable to specific test invocation

### Safety Manual — test_safety_manual.cpp
- `docs/safety_manual.md` documents: assumptions, limitations, configuration rules for ASIL D
- Safety manual covers: scheduler invariants, memory isolation, interrupt latency bounds, watchdog coverage
- Configuration rules specify mandatory settings for each safety level
- Known limitations documented (e.g., OOM policy gap, single-core assumption)

### Traceability Matrix — test_traceability.cpp
- `docs/traceability.csv`: each ISO 26262-6 requirement → design element → code module → test case
- Every requirement in safety manual has at least one test case mapped
- Traceability matrix is machine-readable (CSV)
- CI job validates: no test in matrix without existing test registration

---

## Test-Discipline Rework (SIMULATED → DRIVEN)

**Principle:** a kernel test must DRIVE the system to a state, TRIGGER a real
external event (timer tick / ISR / syscall trap / hardware), then VERIFY the
reaction.  Patterns that are forbidden and must be reworked:

- **set_current impersonation:** `Scheduler::set_current(*fake_task)` then a
  direct blocking call in the test body (`sem.wait()`, `mutex.lock()`,
  `queue.send/receive`, blocking `IPC::send/recv`) so the harness pretends to
  be another task.
- **Direct field mutation:** `task->state = ...`, `task->priority = ...`,
  `task->deadline_ticks = ...`, `task->remaining_ticks = ...`,
  `task->alarm_ticks = ...`, `task->pending_signals |= ...`, `task->magic = ...`.
- **Faked tick:** `Scheduler::on_tick()` / `scan_deadlines()` /
  `rate_monotonic_schedule()` called from the test body.
- **Direct syscall dispatch:** `Syscall::handle(num, ...)` with constructed args
  instead of the real `int $0x80` trap.

**Rework rule of thumb:**
```
create task(s) → Scheduler::add_task → reschedule()/yield_as →
busy-wait { pause | hlt } until the real timer-ISR dispatches them →
assert on the reaction.
```
Reference exemplars: `test_ipc_blocking.cpp` (all), `test_sync.cpp`
`sync_queue_*_blocks_when_*`, `test_preemption_under_syscall.cpp` (all),
`test_zombie_cleanup.cpp`, `test_shell_interaction.cpp`.

### Rework Cookbook (apply to every A-test)

**Setup — two legal shapes:**

1. **Kernel task drives the action** (preferred for kernel primitives /
   syscall handlers):
   ```cpp
   static uint64_t g_result = 0;                       // lambda out-param
   auto *t = TaskControlBlock::create([]() {
       g_result = Syscall::handle(SyscallNumber::X, ...);   // body under test
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
2. **Peer task + harness handshake** (blocking / wakeup semantics):
   ```cpp
   auto *peer = TaskControlBlock::create(peer_lambda, 11, 10);
   Scheduler::add_task(*peer);
   Scheduler::reschedule();
   while (peer->state != TaskState::BLOCKED) asm volatile("pause");
   // ... harness does the wake action ...
   while (peer->state != TaskState::TERMINATED) asm volatile("pause");
   ```

**Pitfalls (all observed in the H2/landmine analysis — MUST respect):**
- **Priority:** harness (init, PID 1) = **10** in testmode.  Test tasks MUST be
  **≥ 11** so the timer ISR dispatches them ahead of the harness.
- **Do NOT `yield_as(single_task)`** — `next_task()` skips `current_task()`, so
  a single test task set current is never dispatched (orphaned READY+not-in-RQ).
  Use a plain `Scheduler::reschedule()` and busy-wait.
- **Do NOT `Scheduler::reschedule()` inside the busy-wait** — reschedule is
  deferred (INV-4); the timer ISR must acquire `scheduler_lock_` uncontended to
  apply the switch.  Busy-wait with `asm volatile("pause")` (or `arch::hlt()`
  when the peer must run).
- **BUGS.md#020 landmine:** a C++ lambda cannot run in user mode; `create_user`
  sets a kernel-address entry that #PFs if a timer tick dispatches it.  For
  syscall-handler tests use a KERNEL task (`create`) and set `page_table_` to a
  `VMM::clone_kernel_pml4()` clone when the handler needs a user task
  (e.g. `BufferPool::alloc`).  Free the clone via `cleanup()` (it frees
  `page_table_`), NOT manually.
- **`create_user` in RQ:** if a user task must exist, give it a real
  infinite-loop entry (`for(;;){ reschedule(); hlt(); }`) and use it only as a
  container — never let it be dispatched into user mode.
- **Cleanup after TERMINATED:** a self-terminated task's trampoline calls
  `Scheduler::terminate` → zombie; the reaper calls `cleanup()`.  The test's own
  `remove_task()+cleanup()+delete` is still required and safe (guarded by
  REAPED) — mirror `test_ipc_blocking.cpp`.
- **ResourceTracker:** keep PMM/MemPool/Task/etc. counters balanced (snapshot
  baseline = no delta).  BufferPool POOL pages are a known +N artifact for every
  buffer_pool test (page lives in the pool, not PMM's free list) — keep the
  delta identical to the container tests around it.
- **Never mutate `task->state/priority/deadline_ticks/remaining_ticks/
  alarm_ticks`** — reach the state through real execution.

### T0 fix recipe (timer/deadline/WCET, 28 tests)
Replace `Scheduler::on_tick()`/`scan_deadlines()` + hand-set `deadline_ticks`
with a real task that overruns a real deadline: its lambda busy-waits past
`period_ticks` (real `arch::Timer::ticks()` loop), the real on_tick ISR /
deadline-monitor detects the overrun; assert `deadline_miss_count` / WCET
overrun.  Fix `test_deadline_miss.cpp` (4) first as the T0 proof.  `deadline_list_*`
container tests stay C.

### T1 fix recipe (alarm via real tick, 5 tests)
Kernel task arms `SYS_ALARM`; real timer ticks fire; the task's signal handler
or a polled flag asserts the alarm arrived; assert the not-expired case before
the deadline.  Covers `test_syscall.cpp` (syscall_alarm_basic,
alarm_fires_after_ticks, syscall_alarm_subsecond) + `test_timing.cpp`
(timer_alarm_delivery, timer_alarm_not_expired).

### T2 fix recipe (PI/PCP/PIP, 11 tests)
LOW (prio 5) + HIGH (prio 20) real dispatched tasks.  LOW holds the mutex /
waits on the sem / fills the queue; HIGH blocks on the same primitive; busy-wait
until HIGH is BLOCKED; assert `LOW->priority == HIGH` (boosted) via the real PIP
chain; HIGH releases → both terminate.  Queue-PIP variants use the sender/
receiver real-dispatch pattern.  **ORPHANED files (dead code, register_* never
called):** test_locking.cpp, test_locking_stress.cpp, test_preemption.cpp,
test_ipc_extended.cpp, test_daemon_restart_crash.cpp — wire into a registered
class + rework, or delete (ASK USER first).

### T3 fix recipe (IPC blocking, 13 tests)
Sender task blocks on a full receiver queue (real `IPC::send` in a dispatched
lambda, prio 11); harness drains → sender wakes → both terminate.  Waiter-list
invariants verified via the real `block_sender`/`wake_sender` IPC path with
dispatched tasks.  `ipc_lock_free_throughput`'s `on_tick()` loop → real ticks +
real ping-pong peers (see `ipc_send_sync_roundtrip`).

### T4 fix recipe (direct syscall dispatch, 41 tests)
Kernel task in a dispatched lambda calls `Syscall::handle(...)` — the handler's
`syscall_task()` resolves to the REAL running task.  Handlers needing a user
task (BUF_*, VFS fd ops) get `page_table_` = clone (BUGS.md#020 note).  Full
`int $0x80` ABI is covered by the ELF userspace harness; only add the trap where
the test explicitly verifies trap/IRQ entry (fuzz bounds can stay kernel-call).
**PROOF DONE (2026-08-03):** `buffer_pool_syscall_dispatch` — kernel task +
`page_table_`=clone + real `add_task`+`reschedule`+busy-wait.  Removes the
BUGS.md#020 user-mode-#PF landmine that hung `all` at test 18.  `buffer_pool`
24/24 ×3; `all` passes tests 18–21 (remaining hang = pre-existing H2 at
`ipc_send_sync_roundtrip` ~78, §v0.3.9).

### T5 fix recipe (process/fork/clone, 16 tests)
Real parent task invokes `SYS_FORK`/`clone` (real syscall in a dispatched
lambda); child runs and exits; parent `SYS_WAITPID` reaps; assert page-table
isolation + FPU state copy on the REAL child after real dispatch.
`idle_task_restartable_on_crash` → terminate idle via the real crash/reap path.

### T6 fix recipe (scheduler/lifecycle field-mutation, 13 tests)
Replace `task->state = BLOCKED` / `x->priority = n` / `scan_deadlines()` with the
real transition: semaphore/mutex → dispatch-driven contention (T2 cookbook);
inrq/RQ consistency → real add/dispatch/remove; hhdm bounds → real user alloc
in a dispatched user task; wcet → real overrun task (T0).

### T7 — Verify gates (run after EVERY group, in order)
1. affected class: `make execute-test x86_64 debug <class>` → 0 failures
2. neighbouring classes sharing the primitive (e.g. after T3: `ipc`,
   `ipc_blocking`, `ipc_robustness`)
3. `make execute-test x86_64 debug selftest` (132/132)
4. `make build` (check-style Errors: 0)
5. append `test-history.txt` row per class
The full debug `all` (881) / `release all` (84) gates are BLOCKED on the H2 race
(§v0.3.9) — do not treat the `all` hang as a rework failure until H2 is
resolved; use the class gates as the rework acceptance.

### Audit summary (968 test functions)
- **A — SIMULATED (rework): 149**
- **B — DRIVEN (keep): 71**
- **C — PURE / container / query / stub (keep): 748**

**Count reconciliation:** the 149 A-tests split into the 6 work groups below
(T0–T6: 28+11+13+41+16+13 = 122 named) plus **29 orphaned dead-code tests**
(`test_locking.cpp` 13, `test_locking_stress.cpp` 4, `test_preemption.cpp` 7,
`test_ipc_extended.cpp` 3, `test_daemon_restart_crash.cpp` 1, and the 2
alarm-overlap duplications in T0/T1).  The orphaned files' `register_*_tests()`
are never called by `test_registry.cpp` — dead code, not in any `all`/class
run; the wire-in+rework-vs-delete decision is tracked under T2.

### T0 — Timer/deadline/WCET cluster (28 tests)
Direct `on_tick()`/`scan_deadlines()` + hand-set deadline fields.  Rework:
real task overruns a real deadline (sleep past period in its lambda); verify
via the deadline-monitor task / on_tick ISR path.

| File | Tests | Class |
|---|---|---|
| test_timing.cpp | timer_tick_accounting, timer_period_reload, timer_alarm_delivery, timer_alarm_not_expired, timer_rate_monotonic_schedule_indirect, timer_reap_orphans_periodic, timer_no_side_effects_on_idle, timer_daemon_restart_not_triggered_on_active, timer_deadline_miss_detection_fires, timer_deadline_miss_skips_future, timer_deadline_miss_only_once, timer_deadline_miss_skips_zero | timing |
| test_deadline_miss.cpp | DeadlineMissWhileBlocked, DeadlineMissWhileTerminatedSkipped, DeadlineRearmOnPeriodRollover, DeadlineMonitorDetectsMiss | deadline_miss |
| test_deadline_action.cpp | DeadlineActionLogOnly, DeadlineActionPanics, DeadlineActionDemote, DeadlineActionKill, DeadlineActionNotifyProbe | deadline_action |
| test_wcet_overrun.cpp | WcetOverrunDetectionFires, DeadlineMissWithinWcet | wcet_overrun |
| test_ss_deadline.cpp | SsExhaustionTriggersDeadline, SsDeadlineMissDuringReplenish | ss_deadline |
| test_deadline_recovery.cpp | DeadlineDetectionMagicCheck, DeadlineDetectionMcdcCoverage, DeadlineActionNotifyMonitor | deadline_recovery |

### T1 — Alarm via real tick (5 tests)
Drive by arming `SYS_ALARM`, let real ticks fire, verify the signal arrives.

| File | Tests | Class |
|---|---|---|
| test_syscall.cpp | syscall_alarm_basic, alarm_fires_after_ticks, syscall_alarm_subsecond | syscall |
| test_timing.cpp | timer_alarm_delivery, timer_alarm_not_expired | timing |

### T2 — PI / PCP / PIP protocol suites (11 + orphans)
Replace set_current + direct lock/wait with real contending tasks.

| File | Tests | Class |
|---|---|---|
| test_priority_inheritance.cpp | MutexPriorityDonates, MutexChainPropagates, MutexPriStepDown, MutexNestedDrop, SemaphoreInherits | priority_inheritance |
| test_queue_pip.cpp | queue_pip_boost_sender, queue_pip_boost_receiver, queue_pip_multiple_senders | lock_protocol / priority_inheritance |
| test_mutex_pcp.cpp | PcpNestedCeilings, PcpCeilingDisabled, PcpPipFallback | lock_protocol / priority_inheritance |

**ORPHANED (dead code — register_* never called):** test_locking.cpp (13),
test_locking_stress.cpp (4), test_preemption.cpp (7), test_ipc_extended.cpp (3),
test_daemon_restart_crash.cpp (1).  Decide: wire into a class + rework, or delete.

### T3 — IPC blocking / waiter manipulation (13 tests)
Drive via real IPC::send/recv in dispatched tasks; wake/termination via the
receiver's actual exit path.

| File | Tests | Class |
|---|---|---|
| test_ipc.cpp | ipc_block_sender_adds_to_list, ipc_wake_sender_removes_from_list, ipc_wake_sender_terminated, ipc_wake_sender_restores_priority, ipc_send_block_full, ipc_sender_unblocked_on_receiver_exit, ipc_send_wakes_blocked_destination | ipc |
| test_ipc_robustness.cpp | IpcConcurrentSenders, IpcBufHandleTransferRoundtrip, IpcBlockedSenderOnReceiverCleanup | ipc_robustness |
| test_ipc_lock_free.cpp | ipc_recv_no_cli, ipc_send_sync_no_cli, ipc_lock_free_throughput | ipc |

### T4 — Direct syscall dispatch (41 tests)
Replace `Syscall::handle(...)` with a real `int $0x80` trap from a userspace
task; verify via the real syscall ABI.

| File | Tests | Class |
|---|---|---|
| test_syscall.cpp | syscall_alarm_basic, syscall_gettod, syscall_uname, alarm_fires_after_ticks, syscall_alarm_subsecond, syscall_dispatch_getpid, syscall_dispatch_invalid_returns_minus_one, syscall_dispatch_get_ticks, syscall_dispatch_yield, syscall_dispatch_print_noop, syscall_fork_returns_pid, syscall_exec_nonexistent, syscall_signal_sigreturn | syscall |
| test_syscall_fuzz.cpp | SyscallFuzzBounds, SyscallFuzzFlags, SyscallFuzzStates, SyscallFuzzPrivilege | syscall |
| test_rlimit.cpp | sys_getrlimit_nofile, sys_getrlimit_stack, sys_getrlimit_data, sys_getrlimit_invalid, sys_brk_query | process |
| test_random_syscall.cpp | syscall_getrandom_basic, syscall_getrandom_zero, syscall_getrandom_large, syscall_getrandom_invalid_flags | random |
| test_vfsd.cpp | vfsd_kernel_bypass_open, vfsd_kernel_bypass_read, vfsd_kernel_bypass_write, vfsd_kernel_bypass_stat, vfsd_kernel_bypass_fstat, vfsd_kernel_bypass_chdir | vfs |
| test_vfsd_auth.cpp | vfsd_self_authorization, vfsd_self_authorization_fd_op, vfsd_absent_authorize_fails, vfsd_absent_syscall_fails, vfsd_authorize_null_path | security |
| test_microkernel_transition.cpp | MinimalPrivilegedSurface, UserspaceDriverIsolation | bench |
| test_signals.cpp | signal_kill_delivers | process |
| test_buffer_pool.cpp | buffer_pool_syscall_dispatch | buffer_pool |

### T5 — Process / fork / clone simulation (16 tests)
Drive clone()/fork/waitpid via real syscalls from a running task; reap via the
real reaper.

| File | Tests | Class |
|---|---|---|
| test_process.cpp | process_clone_adds_child | process |
| test_task.cpp | task_clone_shares_page_tables, task_fork_child_cleanup_preserves_parent_pages, task_clone_no_page_table_leak | scheduler |
| test_task_lifecycle.cpp | task_exit_cleans_all_ipc_objects, task_exit_wakes_blocked_senders, task_reparent_preserves_resources, task_zombie_state_cleanup, scheduler_reap_respects_parent_wait, lifecycle_zombie_no_waker, task_cleanup_frees_msg_queue_with_blocked_senders | scheduler |
| test_waitpid.cpp | waitpid_zombie_over_new_child, waitpid_two_children_sequential_reap, waitpid_cr3_switch_on_status_write | process / safe |
| test_fpu_clone.cpp | fpu_clone_copies_state | fpu_clone |
| test_idle_task.cpp | idle_task_restartable_on_crash | scheduler |

### T6 — Scheduler / lifecycle field-mutation (13 tests)

| File | Tests | Class |
|---|---|---|
| test_scheduler.cpp | scheduler_current_task_after_switch, scheduler_add_duplicate_id | scheduler |
| test_testrunner.cpp | harness_snapshot_inrq_consistency, harness_hhdm_user_page_bounds, harness_buffer_unmap_stale_safe | testrunner |
| test_starvation_deadlock.cpp | PriorityInversionChain5, DeadlockNestedMutexLoad | deadlock |
| test_wcet_scheduler.cpp | wcet_scan_deadlines | wcet |
| test_buffer_pool.cpp | buffer_pool_ipc_transfer, buffer_pool_exec_into_current_clears_buffers, buffer_pool_kernel_task_alloc_fails | buffer_pool |
| test_sync.cpp | semaphore_wait_post, mutex_lock_unlock | vfs |

### T7 — Verification gates
After each group: affected class to 0 failures → debug `all` (881/881) →
`release all` (84/84) → `check-style` Errors: 0 → append test-history.txt rows.

