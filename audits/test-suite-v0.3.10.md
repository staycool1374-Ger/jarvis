# Jarvis RTOS — Test-Suite Audit Report (v0.3.10 contract)

**Audited:** 2026-08-06 | **Branch:** main (post v0.3.12 teardown fix)
**Scope:** all 151 files under `src/kernel/test/` (≈1000 registered tests + benchmarks)
**Reference contracts:** `src/lib/test.hpp` (driven-test cookbook §186-279), `docs/specs/test-harness.md` (§1 discipline, §5 isolation, §6 watchdog), `docs/specs/{scheduler,memory,ipc,vfs,boundary,deadline,configuration,oom-rt,drivers}.md`, `src/kernel/test/test_sched_helpers.hpp`.

**Method:** each test classified against the five audit goals; verdicts PASS / WARN / VIOLATION / TRIVIAL (trivial = stub, tautology, verbatim duplicate, or vacuous). All file:line anchors verified against the current tree by nine parallel review passes.

---

## 1. Executive summary

The suite is large and genuinely driven in its core (IPC blocking, waitpid, deadline, sync teardown), but two systemic defects dominate:

1. **Cookbook Rule-4/5 systemic breach** — the pattern `Scheduler::remove_task()+t->cleanup()+delete t;` applied to **self-terminated** tasks (which the trampoline already routed to the zombie list) plus asserts that run **before** `drain_zombie_list()` appears across ~50 tests. Correct only because `drain_zombie_list()`'s magic-guard heals the dangling zombie head (`scheduler.cpp:213`); a recycled block aliased by the zombie list would be a UAF.
2. **~150 pure-pass stubs / tautologies** — files whose bodies are `JARVIS_TEST_PASS()` only, asserting nothing (deadlock/WFG/stress/capability/GDT/PIC/GIC/PLIC/serial/keyboard/address/bootparams/multiboot, all 8 vfs_internal, mlock, tmpfs-io, etc.).

Secondary issues: helper-API bypass (`release_task()` re-implements the forbidden teardown; `dl_make`/raw `set_current` impersonation instead of `create_test_task()`), direct-ISR / fake-tick simulation (`Timer::handle_irq()`, `DmaEngine::handle_irq()`, `set_ticks_for_test`), spec-vs-assertion mismatches (`scheduler_shorter_period_preferred`, `syscall_fork_returns_pid` tautology, `idt_syscall_handler_installed` LSTAR premise, IPC priority direction), and the vfsd-auth/driver/daemon-restart stub families that mask untested daemon-authorization and crash paths.

**Aggregate verdicts across files (per-file tallies at §5):**

| Metric | Count |
|---|---|
| Files audited | 151 |
| Tests classified | ~1000 |
| PASS (spec+discipline conformant) | ~460 |
| WARN (pattern deviation, benign today) | ~250 |
| VIOLATION (contract breach, must fix) | ~120 |
| TRIVIAL (purge candidate) | ~170 |
| Pure-pass stub tests (assert nothing) | ~150 |

*Counts are lower bounds; several tests carry multiple violations.*

---

## 2. Goal-by-goal findings

### 2.1 GOAL 1 — SPECIFICATION MATCH

Tests must assert behavior the kernel spec documents, and must not contradict it. Findings:

**A. Assertion contradicts the spec (VIOLATION):**
- `scheduler_shorter_period_preferred` (`test_scheduler.cpp:427-447`) — asserts `next_task()` prefers the shorter period at equal priority; `next_task()` is a priority-bitmap + FIFO bucket (`ready_queue_manager.cpp:67-73`), `effective_priority()` returns priority only (`scheduler.cpp:98-116`), and `scheduler.md` has no same-priority period rule. Passes only because `t1` was enqueued first — false-positive.
- `syscall_fork_returns_pid` (`test_syscall.cpp:401,411`) — asserts `g_ret == 0 || g_ret > 0` (tautology; accepts `UINT64_MAX`); kernel returns `-1` for `regs==nullptr` (`syscall_handlers_process.cpp:41-42`). Docstring wrong.
- `idt_syscall_handler_installed` (`test_idt.cpp:64-66`) — x86_64 syscalls use MSR STAR/LSTAR → `syscall_entry` (`syscall.cpp:41-43`), not `int 0x80`; the assertion is vacuous.
- `memory_safety_pmm_free_zero` (`test_memory_safety.cpp:79-82`) — claims `PMM::free_page(0)` is a "safe no-op"; page 0 is in the reserved `[0,kernel_start)` range (`pmm.cpp:88-92`), so the free clears the bitmap, bumps `free_pages_`, and pushes phys 0 onto the free list — a reserved page becomes allocatable. Contradicts `memory.md` reserved-page binding + `oom-rt.md` §3.
- `buffer_pool_deterministic_zero_copy_transfer` (`test_buffer_pool_deterministic.cpp:122`) — buffer VA `0x40000000` equals `kUserYieldStubVa` (`task.cpp:288`), the exact collision `memory.md` §5.3 prohibits; `BufferPool::map` overwrites the stub PTE and orphans the stub page.
- `deadline_list_remove_absent` (`test_timing.cpp:612-628`) — claims "non-member remove is a no-op"; the node *is* a member (inserted at :618), so a non-member removal is never exercised.
- `waitpid_cr3_switch_on_status_write` (`test_waitpid.cpp:194-268`) — claims to validate the CR3 switch in `sys_exit`; never calls waitpid/sys_exit, only hand-rolls two PML4s and toggles `write_cr3()` in the harness. Misleading + hazardous.
- `vfs_pipe_read_write` (`test_vfs.cpp:401-404`) — double `ops->close` then `fd_table.free` → `vnode_ref_dec` on freed vnodes (UAF); violates `vfs.md` §6 close-once. Redundant with `pipe_write_then_read_roundtrip`.
- `memory_determinism_*` (`test_memory_determinism.cpp`) — header claims "task blocked/killed policy" verification; no task is created, and no OOM-handler guard (`oom-rt.md` §3), risking daemon/monitor alloc during exhaustion.
- `sporadic_server_deadline_miss` (`test_sporadic_server.cpp:339-351`) — comment claims "deadline handler fires / handler flag set"; no handler exists; body is a duplicate of `sporadic_server_consumption_exhaustion`.
- `test_sporadic_server.cpp:319-334` — EXHAUSTED `bg_prio=42 > base=1` and `> 10`, contradicting `scheduler.md` §0 (the exact config class of the historical `ss_deadline` hang).
- `DeadlineDetectionMcdcCoverage` (`test_deadline_recovery.cpp:183-247`) — claims 4-condition MC/DC incl. `!deadline_missed=false`; that case is absent; header numbering ≠ code.
- `cross_ipc_queue_priority_ordering` (`test_cross_arch.cpp:422`) — queue "highest = lowest number" (`ipc.cpp:88-90`) is the inverse of `scheduler.md` §0 "higher numeric = higher priority"; internally consistent but spec-divergent.
- `PcpPipFallback` / `PcpCeilingDisabled` (`test_mutex_pcp.cpp`) — the named PIP/ceiling-disabled paths are never exercised (HIGH blocks on a semaphore gate, never on the mutex; no ceiling-field assertion).
- `mutex_priority_inheritance_indirect`, `mutex_priority_chain`, `mutex_waiter_priority_order`, `semaphore_wait_priority_order`, `priority_inversion_under_contention` — names claim PI/ordering coverage; no boost/restore or wake-order assertion exists (waiters block on gates; `g_high_woken==1` passes under any order).
- `daemon_restart_after_cleanup_crash` (`test_daemon_restart_crash.cpp:52-71`) — drives a sequence with a documented latent crash but asserts only task presence, codifying the defect as PASS.
- `pipe_write_to_full_blocks`, `queue_send_receive_block`, `ipc_multiple_blocked_senders_wake_one`, `ipc_userspace_block_uses_sti_hlt_cli`, `ipc_kernel_block_skips_sti` — names/docstrings promise blocking behavior that never occurs (non-blocking paths only, or no task ever blocks).

**B. Doc/spec staleness (WARN):**
- `test-harness.md:54-55` ("never terminate a semaphore-blocked task") now contradicts `test.hpp:213-218` (SAFE since v0.3.12). Spec docs disagree; follow test.hpp.
- `test_memory_safety.cpp:40` claims largest pool is 4480 bytes; real largest is 8192 (`mempool.cpp:41`).
- `vmm_map_already_mapped` encodes a documented [OPEN] remap-orphan hazard as expected behavior; `vmm_huge_page_split_corner` has contradictory enable/disable comments.
- `test_irqguard_audit.cpp:33` claims "only boot/panic use IrqGuard"; `scheduler.md` INV-6 mandates IrqGuard in IPC/scheduler paths — premise false.
- QEMU-machine-specific hard assertions (not general hardware contracts): `test_pci.cpp:68,83,113,152,211,250`; `test_virtio.cpp` requires virtio-net or fails outright.

### 2.2 GOAL 2 — HELPER API USAGE

Tests must use the official helper API, not raw re-implementations. The official surface is `test_sched_helpers.hpp` (`yield_as`, `yield_to_task`, `ScopedCurrentTask`, `create_forever_task`, `terminate_and_drain`, `wait_for_termination`, `trigger_deadline_monitor_scan`, `create_test_task`), `create_named_task`/`add_task_named`, `snapshot_restore`, and the JARVIS macros.

**Violations / raw re-implementations:**
- `release_task()` = `remove_task()+cleanup()+delete` — hand-rolled, rule-4-breaking teardown in `test_ipc_lock_free.cpp:42-48`, `test_queue_pip.cpp:99-105`, `test_locking_stress.cpp:44-50`, `test_syscall.cpp:76-82`, `test_syscall_fuzz.cpp:41-47`, `test_random_syscall.cpp:36-42`, `test_priority_inheritance.cpp:110-116`. Should be `terminate_and_drain()` / `drain_zombie_list()`.
- `register_blocked_receiver` (`test_ipc.cpp:74-77`) and inline `state=BLOCKED; register_task` (`test_ipc_extended.cpp:182-183,411-412`) re-implement the sanctioned `create_test_task()`.
- `dl_make` (`test_timing.cpp:511-530`) — raw `deadline_ticks` write + `Scheduler::dequeue_ready` + `ScopedCurrentTask` impersonation instead of `create_test_task()`.
- `test_jitter.cpp:53-55,131-133` — raw `set_current()+reschedule()` instead of `yield_as()`.
- `test_budget.cpp` — raw `create+cleanup+delete` instead of `create_test_task()` for field-math scenarios.
- `test_atomic.cpp:183-211` — manual `remove_task+cleanup+delete` instead of `terminate_and_drain`.
- `test_hal.cpp:124-146` — manual `PMM::alloc_page/free_page` without RAII.
- `test_cleanup.cpp:56-57` — direct `t->state = TERMINATED` (infrastructure, acceptable).
- `test_syscall.cpp:37` includes `test_sched_helpers.hpp` but uses none of its helpers.

### 2.3 GOAL 3 — COOKBOOK COMPLIANCE

The six mandatory rules (`test.hpp:195-218`). Rule 6 is now satisfied everywhere (v0.3.12 teardown fix). The systemic breaches:

**Rule 4 — `remove_task()+cleanup()+delete` on self-terminated tasks (VIOLATION):**
- `test_scheduler.cpp:368-370`; `test_preemption_under_syscall.cpp:131-138,174-182,212-219,252-262`; `test_atomic_context_switch.cpp:92-98,198-205`; `test_ipc.cpp:917-922`; `test_ipc_blocking.cpp:151-156,220-222,278-280`; `test_ipc_lock_free.cpp:99,174-175`; `test_buffer_pool.cpp:543-548,635-640,875-878`; `test_queue_pip.cpp:162-169,231-238,288-303`; `test_locking_stress.cpp:98-99,162-164`; `test_spinlock.cpp:124-131,181-186`; `test_spinlock_stress.cpp:67-76,92-100`; `test_random_syscall.cpp:83,114,149,174`; `test_priority_inheritance.cpp:158-159,246-248,306-309,384-386,466-467`; `test_testrunner.cpp:218-222,238-242,340-342,520-523`; `test_resource_exhaustion.cpp:131-138`; `test_freelist_consistency.cpp:98-105`; `test_vfsd.cpp:59`; `test_vfsd_auth.cpp:71,112,139,168,194`; `test_fpu.cpp:194-199`; `test_fpu_clone.cpp:104-106`; `test_fpu_multi.cpp:139-147`; `test_fpu_sse.cpp:115-120,205-210`; `test_fpu_xmm_all.cpp:172-177`; plus the deadline/sporadic/timing family (`test_deadline_action.cpp:83-90`, `test_deadline_miss.cpp:79-86`, `test_deadline_recovery.cpp:73-80`, `test_ss_deadline.cpp:75-81`, `test_timing.cpp:64-70,535-540`, `test_wcet_overrun.cpp:81-87`, `test_wcet_scheduler.cpp:83-93`).

**Rule 5 — asserts before cleanup/drain (VIOLATION):** same files/tests as above; also `test_sync.cpp:84,359-361,507,562,619`; `test_locking.cpp:90,151-152,190,269-270,375-376,488-490,525,557,633,691-692,734,808-810,853`; `test_rlimit.cpp:66-71,95-99,123-127,147-149,170-172` (comments claim the opposite of the code); `test_waitpid.cpp:133-138,184-189`; `test_signals.cpp:238-246`; `test_zombie_cleanup.cpp:90,139`; `test_idle_cleanup.cpp:90`; `test_apic_timer.cpp:102-106` (suite-hang risk).

**Rule 2 — cooperating-task registration not under one `arch::IrqGuard` (VIOLATION where a tick can split):** `test_idle_cleanup.cpp:59-68`; `test_scheduler.cpp:206-207,433-434`; `test_preemption.cpp:102`; `test_o1_scheduler.cpp:246-247,261`; `test_idle_task.cpp:120`; `test_ipc_blocking.cpp:107,124` (receiver READY); `test_atomic.cpp:175-181`; `test_testrunner.cpp:96,159,209,226,253,270`; `test_starvation_deadlock.cpp:55,65`; `test_deadline_recovery.cpp:123`; `test_ipc_robustness.cpp:212`; FPU suite (`test_fpu.cpp:167-168`, `test_fpu_multi.cpp:109-111`, `test_fpu_sse.cpp:94-95,180-181`, `test_fpu_xmm_all.cpp:145-146`); `test_signals.cpp:217-232`; the six `test_ipc.cpp` blocked-sender tests (low-risk, receiver pre-BLOCKED).

**Rule 3 — spin on observed state vs bounded polls (WARN):** FPU/MXCSR/XMM suite uses bounded `reschedule()` polls instead of `while (state != …) pause()` and runs at prio 1-4 (below the driven floor of ≥11); `test_testrunner.cpp:118-121,181-186` timed loops.

### 2.4 GOAL 4 — REAL KERNEL DRIVEN

All processing must happen inside the live system; only input simulation (external interrupts) is allowed. Banned: `set_current` impersonation, direct `on_tick()`, fake ticks, direct ISR/handler invocation, direct field/state mutation (sole exception `create_test_task()`).

**VIOLATION — direct ISR / fake-event invocation:**
- `test_cross_arch.cpp:314` — direct `arch::Timer::handle_irq()`.
- `test_dma.cpp:263,322,347` — direct `DmaEngine::handle_irq()`; DMA completion fabricated on an unmapped fake port (`0xFF00` → 0xFF reads).
- `test_cross_arch.cpp:266` — `set_ticks_for_test()` fake-tick backdoor in a test body.
- `test_timer.cpp:75-84` — direct `Timer::handle_irq()`; `:62-68` `set_ticks_for_test`; `:92-102` live PIT reprogramming without restore.
- `test_tcb_write_log.cpp:25-48` — writes `0xDD` into the live current task's magic and calls `cleanup()` on it (frees its own kernel stack mid-execution), then hand-restores.
- `test_atomic_context_switch.cpp:237-272` — direct `scheduler_on_context_switch()` (ISR-epilogue impersonation) + hand-set `scheduler_next_task_id`.
- `test_hal.cpp:124-148` — `ArchContextManager` struct-copy mock; real `switch_to_task`/`TaskContext` path never exercised. `hal_page_table_map_unmap`/`_clone` names promise map/clone; bodies only read CR3.

**VIOLATION — workers never execute (simulated concurrency):**
- `atomic_sb_litmus` (`test_atomic.cpp:145-219`) — workers at prio 5 < harness 10; the ISR epilogue always re-selects the harness (INV-4), so the worker bodies never run; `forbidden_count` trivially 0 and the assert always passes. Masks the real memory-ordering path.

**VIOLATION — simulated hard state:** `test_cross_arch.cpp:334-336` mid-run 8259 re-init (PIC masks cleared, not restored, not snapshot-captured); spurious EOIs (`test_hal.cpp:182-184`, `test_cross_arch.cpp:344-348`).

**WARN — semi-simulated:** `test_sporadic_server.cpp` (pure-object synthetic tick args; acceptable as leaf-object unit, analogous to `create_test_task` exemption); `test_starvation_deadlock.cpp` PriorityInversionChain5 / DeadlockNestedMutexLoad (deadlock simulated via independent semaphore gates; no genuine mutex contention — cross-file contradiction with `test_priority_inheritance.cpp:90-107`, which DOES genuinely block on the mutex); `test_budget.cpp` (no dispatch, field-init); `test_jitter.cpp` (set_current; vacuous under CONFIG_DEBUG_IPC_SCHED); `test_microkernel_transition.cpp` KernelApiPureFunctions / IpcLatencyJitter (harness-side self-send); FPU suite bounded-poll semi-drive.

**Acceptable exemptions:** pure container/data-structure/CPU/math units (`test_lib`, `test_net`, `test_dmesg`, `test_no_op_new`, `test_checked_ptr`, `test_hal_bits`, `test_spsc`, PCI address math, SG/PRD structs, IDT config reads, FAT32 over `MockBlockDevice`), and all TF_RELEASE `safe`-class shell-interaction loopback tests.

### 2.5 GOAL 5 — TRIVIAL TEST PURGE

Purge targets (no safe-class exemption unless noted). ~150 tests assert nothing.

**Pure-pass stubs (assert nothing):**
- `test_capability.cpp` — all 22 (no capability syscall exists).
- `test_gdt.cpp` (5), `test_pic.cpp` (3), `test_gic.cpp` (3), `test_plic.cpp` (3), `test_threaded_irqs.cpp` (3), `test_irq_alloc.cpp` (3), `test_address.cpp` (6), `test_bootparams.cpp` (4), `test_multiboot.cpp` (5), `test_serial.cpp` (4), `test_keyboard.cpp` (5).
- `test_vfs_internal.cpp` — all 8 (duplicate real tests elsewhere).
- `test_mlock.cpp` — all 5 (SYS_MLOCK unimplemented).
- `test_stress.cpp` (6), `test_wfg.cpp` (4), `test_deadlock_detect.cpp` (6), `test_deadlock_recovery.cpp` (6), `test_integration.cpp` (1), `test_tmpfs_io_timeout.cpp` (1), `test_tmpfs_corrupted_metadata.cpp` (1).
- `test_vfsd.cpp` — 11 daemon-auth stubs (the actual `vfsd_authorize` IPC path is untested); `test_iocd.cpp` (7); `test_driver.cpp` (4); `test_health.cpp` (5); `test_gcov.cpp` (4); `test_debug.cpp` (2); `test_textutils.cpp` (1).
- `test_spinlock.cpp:245,263`; `test_ipc_extended.cpp:105,148`; `test_pipe.cpp:56,145`; `test_ipc_benchmark.cpp:149` (TF_BENCH-exempt); `test_cpu_load.cpp:54-60,69-80`; `test_memory.cpp` (empty register stub).

**Tautologies / assert-nothing:**
- `syscall_dispatch_get_ticks` (`test_syscall.cpp:331`) — `g_ret > 0 || true`.
- `syscall_fork_returns_pid` (`test_syscall.cpp:411`) — accepts `-1` too.
- `IpcLatencyJitter` (`test_microkernel_transition.cpp:177`) — `max_lat >= min_lat`.
- `vfsd_absent_syscall_fails` (`test_vfsd_auth.cpp:166-167`) — `g_ret == -1 || g_ret >= 0`.
- `process_num_children_count` (`test_process.cpp:85`) — `(void)…`.
- `lock_order_consistent_nesting` / `lock_order_three_way` (`test_lock_order.cpp:22,71`) — `JARVIS_ASSERT(true)`.
- `buffer_pool_kernel_task_alloc_fails` etc. — `if (!sender){PASS;return}` silent-pass on OOM (`test_ipc_extended.cpp:355`, `test_ipc_robustness.cpp:315`, `test_buffer_pool.cpp:499,501,515,525`).

**Verbatim duplicates:**
- `lifecycle_zombie_no_waker` ≡ `task_zombie_state_cleanup`; `task_cleanup_frees_msg_queue_with_blocked_senders` ≡ `task_exit_wakes_blocked_senders`; `elf_load_init_task_common_called` ≡ `task_elf_load_inits_ipc_objects` (and both claim IPC-object asserts that don't exist).
- `kernel_hlt_idle_still_exists` ≡ `idle_task_created_at_boot`; `idle_task_calls_pause_syscall` misnamed/redundant; `multiple_idle_tasks_prevented` vacuous.
- `MempoolFragmentation` (test_resource_exhaustion) ≡ `mempool_fragmentation` (test_mempool); `slab_reclaim_reallocate` ≡ `pmm_alloc_free`; `timer_deadline_miss_detection_fires`/`_skips_future`/`_skips_zero` ≡ deadline_miss/deadline_recovery; `DeadlineMonitorDetectsMiss` ≡ `DeadlineMissWhileBlocked`; `SsDeadlineMissDuringReplenish` ≡ `SsExhaustionTriggersDeadline`; `ipc_priority_inheritance_send` ≡ `ipc_priority_inversion`; `IpcQueueWraparoundEdge` ≡ `ipc_queue_wrap_around`; `ipc_receive_was_blocked_restores_state` ≡ `ipc_send_recv_self`; `pml4_dump_no_user_entries` ≡ `pml4_clone_clears_user_entries`.
- `fat32_dir_attribute_*`/`fat32_chain_corrupt_*` (7) — compile-time-constant re-asserts.
- `hal_bits` `find_highest_msb`/`find_highest_low_64` and `find_lowest_msb`/`find_lowest_low_64` — byte-identical inputs.
- `preemption_interrupt_enable_disable_cycle` — pure flag toggle; `scheduler_quantum_exhaustion` — redundant with `scheduler_preemptive_priority`.

**Misnamed / overclaiming (fix name or fix behavior):** `idle_task_calls_pause_syscall`, `pipe_write_to_full_blocks`, `queue_send_receive_block`, `syscall_dispatch_reboot`/`_halt` (enum-only), `bench_syscall_latency` (measures no syscalls), `slab_reclaim_pages_returned`/`_free_idempotent` (MemPool never returns pages; body frees once), `page_tables_pool_exhaustion` (never exhausts), `ata_pio_identify`/`ata_pio_read_write_sector` (MockBlockDevice only, never `AtaPioDriver`), `vmm_clone_failure_rollback` (success path only), `hal_page_table_map_unmap`/`_clone`, `irqguard_remaining_sites_validated`, `checked_ptr_valid` (no positive case), `klog_concurrent_readers`/`klog_invalid_buffer_eFault`, `pml4_fork_no_child_corrupt_parent` (zero user entries — bug never reproduced), `timer_rate_monotonic_schedule_indirect` (trivial smoke).

**Safe-class (TF_RELEASE) exemptions — do NOT purge:** `test_lib`, `test_checked_ptr`, `test_block_device`, `test_fat32`, `test_vfs_fat32`, `test_waitpid`, `test_shell_interaction` (all part of the curated `safe` class, `test_registry.cpp:266-277`).

---

## 3. Priority-ranked remediation

### P0 — memory-safety / hang hazards (fix immediately)
1. Rule-4 `remove_task+cleanup+delete` on self-terminated tasks → replace with `drain_zombie_list()`/`terminate_and_drain()` (all sites in §2.3). Highest-value single change.
2. `atomic_sb_litmus` workers never execute — raise to prio ≥11 or convert to a real dispatched concurrency test; today it passes vacuously.
3. `vfs_pipe_read_write` double-close UAF — rewrite to single-close path.
4. `test_tcb_write_log` corrupts the live current task — rewrite as a copy-on-write or dedicated orphan TCB.
5. `memory_safety_pmm_free_zero` — remove (frees reserved page 0).
6. `buffer_pool_deterministic_zero_copy_transfer` VA collision with `kUserYieldStubVa` — move buffer VA above `0x100000000`.
7. `test_daemon_restart_crash` codifies a latent crash as PASS — fix the underlying daemon-restart bug or gate the test `#if 0`.
8. `apic_timer_oneshot`/`_stop_restart` can permanently kill the system tick — restore periodic state in-test.

### P1 — driven-test discipline (fix to green contract)
9. Rule-5 assert-before-cleanup reorder across all §2.3 files.
10. Rule-2 single-IrqGuard registration across §2.3 files.
11. Replace direct `Timer::handle_irq` / `DmaEngine::handle_irq` / `set_ticks_for_test` with real ISR or a sanctioned driver-level mock.
12. Replace `release_task()` / `dl_make` / raw `set_current` with official helpers (`terminate_and_drain`, `create_test_task`, `yield_as`).

### P2 — spec & documentation alignment
13. Fix/remove spec-contradicting assertions: `scheduler_shorter_period_preferred`, `syscall_fork_returns_pid`, `idt_syscall_handler_installed`, `deadline_list_remove_absent`, `waitpid_cr3_switch_on_status_write`, `PcpPipFallback`/`PcpCeilingDisabled`, IPC priority direction doc, `sporadic_server` bg_prio=42 config, `DeadlineDetectionMcdcCoverage`.
14. Update `test-harness.md:54-55` to match v0.3.12 test.hpp rule 6 (external termination now safe).
15. Reconcile `test_starvation_deadlock.cpp` vs `test_priority_inheritance.cpp` on whether `Mutex::lock()` can genuinely block a dispatched task.
16. Replace QEMU-machine-specific hard asserts (`test_pci`, `test_virtio`) with capability-gated probes.

### P3 — purge (per §2.5; exempt safe class)
17. Delete all pure-pass stub tests + tautologies + verbatim duplicates listed in §2.5 (≈150 tests).
18. Register stubs for the 40 `register_*_tests` symbols missing from `test_weak_stubs.cpp`.

---

## 4. Statistics (by audit batch)

| Batch (files) | PASS | WARN | VIOLATION | TRIVIAL |
|---|---|---|---|---|
| sync/locking/pip/pcp/spinlock (11) | 36 | 3 | 32 | 29 |
| scheduler/task/lifecycle/preemption/o1 (13) | 42 | 18 | 11 | 12 |
| memory/pmm/vmm/mempool/bufferpool (15) | 44 | 34 | 6 | 14 |
| ipc/vfs/tmpfs/fat32/fstab (17) | 99 | 26 | 7 | 25 |
| deadline/sporadic/wcet/timing/budget (11) | 24 | 42 | 9 | 32 |
| arch/hal/IRQ/dma/pci (22) | 46 | 28 | 12 | 50 |
| process/syscall/signals/fpu/capability (16) | 31 | 35 | 3 | 34 |
| lib/driver/vfsd/net/block_device (21) | 78 | 28 | 37 | 34 |
| random/misc/stress/bench/shell (26) | 73 | 14 | 16 | 31 |
| **TOTAL** | **473** | **228** | **133** | **261** |

---

## 5. Per-file verdicts

See the nine batch reviews referenced in §1; the full per-test classification is preserved in each batch's review (sync/locking, task/scheduler, memory/vmm, ipc/vfs, deadline/timing, arch/hal, process/syscall, lib/driver/vfsd, misc/stress). Representative per-file tallies:

| File | PASS | WARN | VIOL | TRIV |
|---|---|---|---|---|
| test_locking.cpp | 0 | 0 | 13 | 0 |
| test_queue_pip.cpp | 0 | 0 | 3 | 0 |
| test_spinlock.cpp | 7 | 0 | 2 | 6 |
| test_preemption_under_syscall.cpp | 0 | 0 | 4 | 0 |
| test_atomic_context_switch.cpp | 0 | 2 | 3 | 1 |
| test_idle_cleanup.cpp | 0 | 0 | 1 | 0 |
| test_tcb_write_log.cpp | 0 | 0 | 1 | 0 |
| test_memory_safety.cpp | 2 | 2 | 1 | 0 |
| test_buffer_pool_deterministic.cpp | 1 | 2 | 1 | 2 |
| test_vfs.cpp | 14 | 5 | 1 | 0 |
| test_vfs_internal.cpp | 0 | 0 | 0 | 8 |
| test_ipc.cpp | 15 | 6 | 1 | 1 |
| test_ipc_blocking.cpp | 0 | 0 | 3 | 1 |
| test_deadline_recovery.cpp | 0 | 2 | 2 | 2 |
| test_timing.cpp | 1 | 11 | 6 | 8 |
| test_sporadic_server.cpp | 22 | 2 | 1 | 6 |
| test_gdt.cpp / test_pic.cpp / test_gic.cpp / test_plic.cpp / test_serial.cpp / test_keyboard.cpp / test_address.cpp / test_bootparams.cpp / test_multiboot.cpp / test_threaded_irqs.cpp / test_irq_alloc.cpp | 0 | 0 | 0 | 3–6 each |
| test_hal.cpp | 4 | 4 | 3 | 3 |
| test_dma.cpp | 6 | 3 | 2 | 1 |
| test_syscall.cpp | 0 | 10 | 1 | 3 |
| test_capability.cpp | 0 | 0 | 0 | 22 |
| test_vfsd.cpp | 1 | 6 | 11 | 11 |
| test_iocd.cpp | 1 | 0 | 7 | 7 |
| test_health.cpp | 0 | 0 | 5 | 5 |
| test_driver.cpp | 2 | 0 | 4 | 4 |
| test_stress.cpp / test_wfg.cpp / test_deadlock_detect.cpp / test_deadlock_recovery.cpp | 0 | 0 | 0 | 4–6 each |
| test_random_syscall.cpp | 0 | 0 | 4 | 0 |
| test_priority_inheritance.cpp | 0 | 1 | 5 | 0 |
| test_testrunner.cpp | 4 | 2 | 5 | 0 |
| test_freelist_consistency.cpp | 0 | 2 | 1 | 1 |
| test_mlock.cpp | 0 | 0 | 0 | 5 |

*(Files omitted from the table are predominantly PASS-conformant unit/container tests: test_lib 15/15, test_dmesg 15/15, test_spsc 8/8, test_no_op_new 6/6, test_net 5/5, test_tmpfs 6/6, test_vfs_fat32 13/13 + 1 warn, test_fat32 33/33 + 7 trivial, test_block_device 8/8 + 3 warn.)*
