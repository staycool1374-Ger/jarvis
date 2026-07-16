# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 4 HARD REAL-TIME MODE
**Status:** ACTIVE — Strict Deadline Adherence.
**Target Focus:** v0.3.2 — Strict Deadline Adherence: deadline miss detection & handler, budget enforcement with hard preemption, time-partitioning (ARINC 653).

## 1. Safety & Concurrency Guardrails (Strict)
- **Transition to Fine-Grained Locks:** All new synchronization code must use `SpinLock` + `SpinLockGuard` for short critical sections and `sync::Mutex` (without IrqGuard) for blocking paths. The global `IrqGuard` is deprecated for all uses except boot, panic, and test isolation.
- **Reference-Enforced Tasks:** When manipulating task blocks or IPC endpoints within the new init system or system calls, strictly enforce reference passing over raw pointers to prevent dangling lookups.
- **Zero-Allocation tmpfs Operations:** Ensure the initial `tmpfs` implementation relies on the pre-existing fixed `MemPool` / `BufferPool` infrastructure for its nodes to avoid unbounded allocations that violate resource tracking limits.

## 2. Refactoring Phase Strategy: v0.12.14 Execution
When implementing or refactoring code paths for this phase, execute the following steps in sequence:

### Step A: Memory Space Integrity (`SYS_BRK`)
1. Before modifying the Virtual Memory Manager (VMM) to accommodate heap expansion via `SYS_BRK`, verify that page-table cloning invariants are fully preserved.
2. Ensure that any expansion maps correctly into the caller's page tables and is strictly accounted for in `kernel::test::ResourceTracker`.

### Step B: PID 1 & Service Initialization Lifecycle
1. When constructing the init daemon system (`/etc/rc` parser and user quotas), isolate its lifecycle tracking from standard transient userspace applications.
2. Involuntary preemption must remain active and safe during initial daemon forks. Track scheduling states using the `debug_switch_ring` if unexpected page faults occur during `sys_clone` executions.

## Phase 3: System Services & Hardware (v0.12.14–v0.2.25) — Completed

---

## Phase 4: Hard Real-Time (0.3.x)
##v0.3.x — Hard Real-Time Compliance Redesign (Detailed Step-by-Step Tasks)
Executive Summary
Current state: O(1) 128-level priority bitmap scheduler implemented — `next_task()` O(1) WCET, `ReadyQueueManager` with intrusive `TaskQueue` per priority, `PriorityMap` lock-free bitwise operations, lazy rebuild fallback for edge cases. All 13 raw `state = READY` assignments replaced with `Scheduler::set_task_ready()`. Still soft real-time: no deadline enforcement, no PIP, no budget preemption, PIC-based interrupts. Target: Hard real-time per ISO 26262 ASIL D / IEC 61508 SIL 4.

Notes
- All changes must pass make execute-test x86_64 debug all (754/754) before each release
- ResourceTracker must show zero leaks in all hard-RT tests
- Renode simulation for ARM64/RISC-V64 required before M3
- CONFIG_HARD_REAL_TIME=0 builds must remain functionally identical to v0.2.21 (Soft-RT compatibility)

### 0.3.2 Strict Deadline Adherence — Zero-Tolerance (Pillar 2)

#### Pre-Requisite: Bugfix & Stabilisation
- [x] **Fix 15 pre-existing FAILs** in vfsd/iocd/buffer_pool — all resolved (daemon preservation, buffer_pool overflow, PRE annotations)
- [x] **Stash audit: drop superseded stashes** (`stash@{0}`, `stash@{2}`), keep `stash@{1}` for selective extraction
- [x] **Re-enable `DeadlockNestedMutexLoad`** — rewritten with `ScopedCurrentTask` + direct BLOCKED pattern; `add_waiter` now asserts on duplicate; `wake_one` null-safe

#### Deadline Adherence

The deadline miss detection infrastructure already exists in basic form (TCB fields `deadline_ticks`/`deadline_missed`/`deadline_miss_count`, weak handler with 4 config actions, `on_tick()` scan, snapshot/restore). The following phases extend it to production-hardened, ISO 26262-ready detection. Ordered by dependency — each phase builds on the prior.

##### Phase 1 — Architectural Placement & Execution Context (P1)

**Goal:** Fix where and how detection runs. Ensure BLOCKED/WAITING tasks are monitored, periodic deadlines re-arm, and locking is safe.

- [x] **P1a — Extend detection to all non-TERMINATED states** (`scheduler.cpp:on_tick()`)
  - Remove the `state == RUNNING || state == READY` guard on the deadline check
  - BLOCKED and WAITING tasks must not silently blow through their deadline
  - Keep `remaining_ticks`/`executed_ticks` accounting scoped to RUNNING/READY only (no change)
- [x] **P1b — Re-arm `deadline_ticks` on period rollover** (`scheduler.cpp:on_tick()`)
  - When `remaining_ticks` wraps from 0 to `period_ticks`, also: `task->deadline_ticks += task->period_ticks` and clear `task->deadline_missed`
  - Enables periodic tasks to have per-period deadline enforcement
- [x] **P1c — Locking audit: ISR vs scheduler_lock_ race** (`scheduler.cpp:on_tick()`)
  - `on_tick()` runs in ISR context (IF=0) but accesses TCB fields without `scheduler_lock_`
  - On UP: the ISR preempts any task including one inside `add_task()` which holds `scheduler_lock_`
  - Since SpinLock does not disable IRQs, `on_tick()` could read mid-mutation
  - **Fix:** Move deadline detection to a `scheduler_lock_.try_lock()` critical section

**Test addition:**
- [x] `deadline_miss_while_blocked` — task sets deadline, blocks (WAITING), ticks advance past deadline → handler fires
- [x] `deadline_rearm_on_period_rollover` — periodic task re-arms `deadline_ticks += period_ticks` on wrap; `deadline_missed` cleared
- [x] `deadline_miss_while_terminated_skipped` — TERMINATED task never triggers handler

##### Phase 2 — O(N) Deadline Scan (Simplified)

**Note:** The ready queue (`ReadyQueueManager` + `PriorityMap` bitmap) is already O(1) for dispatch. The deadline scan in `on_tick()` linearly iterates `tasks_[]` — at `MAX_TASKS=64` and 1000 Hz this is ~64K checks/sec, <1M instructions/sec. No optimisation needed.

**Changes vs current code:**
- [x] **P2a — Remove stale `state == RUNNING || state == READY` guard duplication** (solved by P1a already moving the check outside the state-filtered block)
- [x] **P2b — Ensure deadline check remains after P1a restructuring**
  - The check now runs on all non-TERMINATED tasks
  - No new data structures; the linear scan remains

##### Phase 3 — WCET Overrun vs Deadline Miss (P2 — Time Representation)

**Goal:** Distinguish execution-time overrun (task consumed >WCET budget) from deadline miss (task didn't finish in time due to interference).

- [x] **P3a — Add WCET overrun detection** (`task.hpp`, `scheduler.cpp`)
  - `task->wcet_ticks` already exists (initialized 0 at create)
  - When `wcet_ticks > 0 && executed_ticks > wcet_ticks`, call weak `wcet_overrun_handler()` (separate from deadline handler)
- [x] **P3b — Add `CONFIG_WCET_OVERRUN_DETECTION`** (`jarvis_config.h`, default 1)
  - Separate compile-time flag so WCET tracking is optional (zero overhead when disabled)
- [x] **P3c — Handler signature**
  - `__attribute__((weak)) wcet_overrun_handler(TaskControlBlock*, uint64_t overrun_by_ticks)` — same `CONFIG_DEADLINE_ACTION` configurability

**Test addition:**
- [x] `wcet_overrun_detection_fires` — task with wcet_ticks=10, executed_ticks=11 fires overrun handler but NOT deadline handler
- [x] `deadline_miss_within_wcet` — task meets WCET but misses deadline (priority inversion) — deadline handler fires, overrun does NOT

##### Phase 4 — SporadicServer Budget Integration (P3 — State Management)

**Goal:** For SS tasks, distinguish server budget exhaustion from a true deadline miss. Pass SS context to the handler.

- [x] **P4a — Pass SS state to handler** (`scheduler.cpp:on_tick()`)
  - When deadline miss fires for an SS task, store `sporadic_server->state()` and `remaining_budget()` on TCB before calling handler
- [x] **P4b — Handler distinguishes exhaustion** (`scheduler.cpp:deadline_miss_handler()`)
  - Default handler logs "server budget exhaustion (state=EXHAUSTED)" vs "deadline miss (budget remaining=X)"
- [x] **P4c — Optional exhaustion→deadline mapping** (`scheduler.cpp:on_tick()` SS consume loop)
  - Add `CONFIG_SPORADIC_SERVER_EXHAUSTION_IS_DEADLINE` — when set, consume() return false fires handler with EXHAUSTED context

**Test addition:**
- [x] `ss_exhaustion_triggers_deadline` — SS task with C=3, T=100, exhausted via direct SS consume + P1a deadline in past → handler fires with EXHAUSTED context
- [x] `ss_deadline_miss_during_replenish` — SS task misses deadline while budget=0 waiting for replenishment, handler fires with EXHAUSTED context

##### Phase 5 — Asymmetric Recovery & Safety Protocols (P4 — Deterministic Error Handling)

**Goal:** Ensure recovery actions are safe and complete. Add NOTIFY_MONITOR action. Add structural isolation for ISO 26262.

- [x] **P5a — Safe KILL action** (`scheduler.cpp:deadline_miss_handler()`, action=3)
  - Replace `task->state = TERMINATED` with deferred-kill list: handler sets TERMINATED + adds to list; `on_tick()` calls `process_deferred_kills()` after try_lock release
  - `process_deferred_kills()`: `Scheduler::remove_task()`, `task->cleanup()`, `delete task`
  - For SS tasks: also `sporadic_server->on_completion()`
  - Lock verified: handler runs under try_lock, deferred cleanup runs after unlock
- [x] **P5b — Add NOTIFY_MONITOR action** (`jarvis_config.h`, `scheduler.cpp`)
  - `CONFIG_DEADLINE_ACTION == 4`: send SIGUSR1 to `CONFIG_DEADLINE_MONITOR_PID`
  - `CONFIG_DEADLINE_MONITOR_PID` added to `jarvis_config.h` (default 0 = disabled)
  - Handler does NOT kill or demote — just notifies supervisor
- [x] **P5c — Structural isolation** (`scheduler.cpp`)
  - Existing magic guard (`if (magic != TCB_MAGIC) continue`) retains skip behavior
  - `deadline_detection_integrity` atomic counter — incremented on each successful scan after the for loop; tests verify counter advances when scan completes

**Test addition:**
- [x] `deadline_action_kill_cleans_up` — action=3 sequence: `cleanup()` + `remove_task()` + `delete`, verified by snapshot/restore leak detection
- [x] `deadline_action_notify_monitor` — SIGUSR1 delivered to monitor via `pending_signals` bit
- [x] `deadline_detection_magic_check` — corrupted TCB magic does not trigger handler; integrity counter advances
- [x] `deadline_detection_mcdc_coverage` — all 4 MC/DC conditions of `period_ticks>0 && !deadline_missed && deadline_ticks>0 && ticks>deadline_ticks` toggled independently (5 cases)

##### Phase 6 — Deadline Monitor Task (Roadmap Item)

**Goal:** Decouple deadline scanning from the timer ISR via a dedicated watchdog task at highest priority.

- [x] **P6a — Add `CONFIG_DEADLINE_MONITOR_TASK`** (`jarvis_config.h`, default 1)
  - When >0, scheduler spawns `[deadline-mon]` at priority 127 during `init()`
  - Entry loop: wait on `s_deadline_scan_sem` semaphore → call `Scheduler::scan_deadlines()` → sleep
- [x] **P6b — Add `Scheduler::scan_deadlines()`** (`scheduler.cpp`)
  - Identical deadline detection logic but runs in task context
  - Can hold `scheduler_lock_`, can call blocking cleanup (safe KILL)
  - Guarded by `#if CONFIG_DEADLINE_MONITOR_TASK`
- [x] **P6c — ISR→task handoff** (`scheduler.cpp:on_tick()`)
  - If monitor task enabled: `on_tick()` posts semaphore instead of inline scanning
  - If disabled: keep inline detection (Phase 1 logic)
- [x] **P6d — Snapshot/restore** (`test_isolate.cpp`)
  - Monitor task is a daemon — preserve across snapshot_restore or re-spawn on restore
  - Exclude from snapshot (like idle_task_); re-spawn on restore

**Test addition:**
- [x] `deadline_monitor_task_spawned` — with CONFIG_DEADLINE_MONITOR_TASK=1, verify monitor present at priority 127
- [x] `deadline_monitor_detects_miss` — task misses deadline, monitor detects within 1 tick

##### Phase 7 — Full WCET Benchmark & MC/DC Coverage

**Goal:** Verify everything. Ensure detection logic has 100% MC/DC coverage and WCET benchmarks pass.

- [x] **P7a — MC/DC test suite** (`test_timing.cpp`)
  - Cover each condition independently: `!missed`, `deadline>0`, `ticks>deadline`, `state!=TERMINATED`
  - Handler actions: LOG, PANIC, DEMOTE, KILL, NOTIFY_MONITOR each verified
  - Deadline queue: sorted insert, remove, pop_earliest, empty, full
- [x] **P7b — WCET benchmark** (`test_wcet_scheduler.cpp`, new file per ROADMAP.md §0.3.8)
  - Measure max cycles of `scan_deadlines()` with 1, 10, 40 tasks over 300 iterations
  - Recorded in `docs/wcet_analysis.md` (QEMU virtual-TSC: ~2k/9k/13k ticks; ~20k at MAX_TASKS=64)
- [x] **P7c — Update expected counts** (`test_expected_counts.hpp`)
  - New class counts added: `deadline_miss=5`, `wcet_overrun=2`, `ss_deadline=2`, `deadline_recovery=4`, `deadline_action=1`, `wcet=1`
  - All class count updated to 764
- [x] **P7d — Regression: 764/764 PASS** — full `all` suite passes on all meaningful deadline/WCET config combinations (except expected DACT=1 PANIC failures)

### 0.3.3 — Inheritance & Ceiling
## Preemptive Priority-Based Scheduling + Priority Inversion Mitigation (Pillar 3)

#### Pre-Requisite: Enable Starvation/Deadlock Tests
- [x] **`DeadlockNestedMutexLoad`:** re-enabled with ScopedCurrentTask + direct BLOCKED pattern; duplicate add_waiter assertion; wake_one null-safe
- ~~**`DeadlockRecoveryResourceReclamation`:** test class never implemented — placeholder removed~~

#### Priority Inheritance & Ceiling
- [ ] Priority Inheritance Protocol (PIP) — Mutex
  - [ ] Add Mutex::priority_ceiling (static ceiling = max priority of any task that may lock)
  - [ ] In Mutex::lock(): if contested, boost owner to max(owner_prio, waiter_prio) via Scheduler::boost_priority()
  - [ ] In Mutex::unlock(): restore owner to base_priority (or highest held ceiling)
  - [ ] Add CONFIG_MUTEX_PIP (default 1) — disable for soft-RT builds
  - [ ] Add CONFIG_MUTEX_PRIORITY_CEILING — compile-time ceiling validation
- [ ] Priority Inheritance — Semaphore
  - [ ] Same pattern: Semaphore::lock_ already exists; add owner tracking + PIP
  - [ ] Binary semaphore: single owner; counting: track last waiter priority for boost
  - [ ] Add CONFIG_SEMAPHORE_PIP
- [ ] Priority Inheritance — Message Queue
  - [ ] Queue::send_waiters / recv_waiters — boost receiver when high-prio sender blocks
  - [ ] Boost sender when high-prio receiver blocks (rare but possible)
  - [ ] Add CONFIG_QUEUE_PIP
- [ ] Priority Ceiling Protocol (PCP) — Optional Stronger Guarantee
  - [ ] Add CONFIG_PRIORITY_CEILING_PROTOCOL — mutex ceiling = static max priority of all users
  - [ ] On lock: system ceiling = max of all held mutex ceilings; block if task prio ≤ system ceiling
  - [ ] Prevents deadlock + chained blocking
- [ ] Scheduler Preemption Points — Verify All
  - [ ] Audit every cli/sti pair — ensure preemption check at each sti
  - [ ] Scheduler::reschedule() called from: syscall return, ISR exit, on_tick, explicit yield
  - [ ] Add CONFIG_PREEMPTION_LATENCY_MAX_CYCLES — measure and assert in test

### 0.3.4 Minimal & Known Interrupt Latency Jitter (Pillar 4)
- [ ] Replace PIC with APIC/x2APIC (x86_64)
  - [ ] Create arch/x86_64/hal/apic.hpp + apic.cpp — Local APIC timer, IPI, TSC-deadline mode
  - [ ] APIC::init() — calibrate TSC, configure timer in one-shot/periodic mode
  - [ ] APIC::set_timer_oneshot(ns) / periodic(ns) — nanosecond resolution
  - [ ] Per-CPU timer interrupt vector (not shared PIC IRQ0)
  - [ ] CONFIG_USE_APIC_TIMER (default 1 on x86_64)
- [ ] Interrupt Latency Measurement & Bounding
  - [ ] Add IRQ_LATENCY_HISTOGRAM (64 buckets, 0-100μs) — record at ISR entry via rdtsc
  - [ ] CONFIG_IRQ_LATENCY_MAX_NS — assert in debug if exceeded
  - [ ] ISR entry/exit stubs in isr_stubs.asm — save rdtsc immediately, no C++ prologue
- [ ] Deferred Interrupt Handling (Threaded IRQs)
  - [ ] CONFIG_THREADED_IRQS — ISR does minimal ack + enqueue to per-IRQ kernel task
  - [ ] IRQ threads: fixed priority (configurable), dedicated stack, no blocking syscalls
  - [ ] IrqThread::init(vector, priority, handler) — replace IDT::register_handler for enabled IRQs
- [ ] ARM64 / RISC-V64 Interrupt Controllers
  - [ ] arch/aarch64/hal/gic.hpp — GICv3/v4 driver, priority masking, SGI/PPI/SPI
  - [ ] arch/riscv64/hal/plic.hpp — PLIC driver, priority levels, threshold
  - [ ] Common ArchInterruptController interface: init, eoi, mask, unmask, set_priority, get_priority


### 0.3.5 Deterministic Memory & Resource Management (Pillar 5)
## Static Memory Pools — Zero Dynamic Allocation After Init
  - [ ] CONFIG_STATIC_POOLS_ONLY — disable PMM::alloc_page() after kernel::init() complete
  - [ ] Pre-allocate ALL kernel structures at boot: TCB array, page tables, IPC buffers, driver rings
  - [ ] MemPool — replace PMM-backed growth with fixed compile-time pools (from v0.2.21 config)
  - [ ] Add MemPool::reserve(pool_idx, count) — called once at init, fails if insufficient
- [ ] Stack Allocation — Fixed, Guarded, No Growth
  - [ ] CONFIG_TASK_STACK_SIZE per-priority (array in config)
  - [ ] Stack guard page (unmapped) below each kernel stack — page fault on overflow
  - [ ] CONFIG_STACK_OVERFLOW_HOOK — weak symbol, called from #PF handler
  - [ ] Compile-time stack usage analysis: -fstack-usage + script to verify ≤ CONFIG_TASK_STACK_SIZE
- [ ] Page Tables — Static, No Fork-Time Copy
  - [ ] Remove clone() page-table sharing (page_table_shared_) — full copy or static assignment
  - [ ] CONFIG_MAX_PROCESS_PAGES — bound user page tables per task
  - [ ] Pre-allocate page-table pages from dedicated PMM pool (no general PMM alloc in hot path)
- [ ] Buffer Pool / IPC — Zero-Copy, Fixed Rings
  - [ ] BufferPool — pre-allocated ring of fixed-size buffers, no alloc/free after init
  - [ ] CONFIG_BUFFER_POOL_BLOCKS per size class
  - [ ] IPC messages: inline payload (no heap), buffer handle for zero-copy
- [ ] Eliminate operator new/delete from Kernel
  - [x] **Apply `lib/new.cpp` rewrite (stash@{3}):** fix `operator delete` calling `MemPool::free` on PMM-backed allocations — added `PmmAllocHdr` header + `MemPool::contains()` check (commit 175f301)
  - [ ] Replace all new/delete with MemPool::alloc/free or placement new into static storage
  - [ ] Add -fno-exceptions -fno-rtti -fno-threadsafe-statics to kernel CFLAGS
  - [ ] Audit lib/stdcpp.cpp — remove ::operator new implementations


### 0.3.6 Cross-Architecture Hard Real-Time HAL
## x86_64 — Complete APIC + TSC-Deadline
  - [ ] arch/x86_64/hal/apic.hpp/.cpp — Local APIC, I/O APIC, TSC-deadline timer, keep in mind to check invariant of TSC (CPUID.80000007H:EDX[8])
  - [ ] arch/x86_64/hal/tsc.hpp — Invariant TSC calibration, rdtsc/rdtscp wrappers
  - [ ] arch/x86_64/hal/msr.hpp — MSR read/write for IA32_TSC_DEADLINE, IA32_TSC_AUX
- [ ] ARM64 — GICv3 + Generic Timer
  - [ ] arch/aarch64/hal/gic.hpp/.cpp — GICD/GICR/GICC init, priority, affinity routing
  - [ ] arch/aarch64/hal/timer.hpp/.cpp — ARM Generic Timer (CNTVCT_EL0, CNTPCT_EL0)
  - [ ] arch/aarch64/hal/context.hpp — AArch64 register save/restore, FP/SIMD
  - [ ] arch/aarch64/boot.cpp — EL2→EL1 transition, MMU init, GIC init
- [ ] RISC-V64 — PLIC + CLINT/S-Mode Timer
  - [ ] arch/riscv64/hal/plic.hpp/.cpp — PLIC init, priority, threshold, claim/complete
  - [ ] arch/riscv64/hal/timer.hpp/.cpp — CLINT (mtime/mtimecmp) or SBI timer
  - [ ] arch/riscv64/hal/context.hpp — RISC-V register save/restore, FP
  - [ ] arch/riscv64/boot.cpp — M-mode→S-mode, PMP, MMU (Sv39/Sv48), PLIC init
- [ ] Common Hard-RT HAL Interface
  - [ ] arch/hal/hard_rt.hpp — Pure virtual interface: Timer, InterruptController, Context, IPI
  - [ ] Compile-time selection via CONFIG_ARCH_* — no runtime polymorphism overhead
  - [ ] WCET annotations on all HAL functions (comments with measured max cycles)

### 0.3.7 Configuration & Validation (v0.2.21 Config System Integration)
- [ ]  Hard-RT Config Profile
  - [ ] Add CONFIG_HARD_REAL_TIME (default 0) — enables all PIP, static pools, APIC, deadline miss handler
  - [ ] When enabled: force CONFIG_PREEMPTION=1, CONFIG_MUTEX_PIP=1, CONFIG_STATIC_POOLS_ONLY=1, CONFIG_USE_APIC_TIMER=1
  - [ ] Add CONFIG_WCET_ANALYSIS — build with -fstack-usage -ftime-report, generate wcet_report.txt
- [ ]  check-config Extensions
  - [ ] Validate: CONFIG_MAX_TASKS ≤ CONFIG_ID_TABLE_SIZE / 2
  - [ ] Validate: CONFIG_STACK_SIZE × CONFIG_MAX_TASKS < CONFIG_KERNEL_HEAP_SIZE
  - [ ] Validate: CONFIG_TICK_HZ divides CONFIG_TIMER_CLOCK_HZ evenly (PIT/APIC)
  - [ ] Validate: Sporadic Server C ≤ T for all configured servers
  - [ ] Validate: Priority ceiling ≥ max task priority for each mutex
  - [ ] Validate: CONFIG_IRQ_LATENCY_MAX_NS < CONFIG_MIN_TASK_PERIOD_NS

### 0.3.8 Test & Verification Suite

#### Infrastructure
- [x] **`vfs_touched` flag (Option B):** lazy daemon restart — skip `reload_daemon_tasks()` for non-VFS tests (~95%), reducing per-test overhead ~70-80%
  - [x] Add `g_vfs_touched` flag in test framework, reset on snapshot_restore
  - [x] Call `mark_vfs_touched()` in all VFS syscall handlers (+ in vfs.cpp core functions)
  - [x] Branch in `snapshot_restore()`: skip daemon restart if flag clear
  - [~] Force flag in daemon crash tests — daemon crash tests still disabled (#if 0), no-op
- [x] **Fix `make execute-test`** — trace Makefile rule, restore expect runner (confirmed working — used by config matrix script)
- [x] **Fix healthcheck.sh** — add per-check labels for actionable failure output
- [ ] **Disabled/stub test remediation:** triage ~90+ disabled/stub/broken tests
  - [ ] Re-enable or remove disabled tests (`#if 0`, registration-commented)
  - [ ] Implement real bodies for stub tests (capabilities, O_NONBLOCK, signal delivery, etc.)
  - [ ] Track remaining with blocker issues

#### WCET & Hard-RT Tests
- [ ] WCET Measurement Tests
  - [ ] test_wcet_scheduler.cpp — measure next_task(), reschedule(), switch_to_task() cycles (10k iterations)
  - [ ] test_wcet_ipc.cpp — measure send/receive latency (same core, cross-core via IPI)
  - [ ] test_wcet_interrupt.cpp — measure IRQ entry→handler→exit latency (histogram)
  - [ ] test_wcet_memory.cpp — measure MemPool::alloc/free, VMM::map/unmap cycles
- [ ] Priority Inversion Tests
  - [ ] test_priority_inversion_mutex.cpp — classic 3-task inversion, verify PIP bounds blocking
  - [ ] test_priority_inversion_semaphore.cpp — same for semaphore
  - [ ] test_priority_inversion_queue.cpp — same for message queue
  - [ ] test_chained_blocking.cpp — 5-task chain, verify PCP prevents deadlock
- [x] Deadline Miss Tests
  - [x] `test_deadline_miss.cpp` — 5 tests covering `deadline_miss_while_blocked`, `deadline_rearm_on_period_rollover`, `deadline_miss_while_terminated_skipped`, `DeadlineMonitorTaskSpawned`, `DeadlineMonitorDetectsMiss`
  - [x] `test_wcet_overrun.cpp` — 2 tests: `WcetOverrunDetectionFires`, `DeadlineMissWithinWcet`
  - [x] `test_deadline_recovery.cpp` — 4 tests: `DeadlineActionKillCleansUp`, `DeadlineDetectionMagicCheck`, `DeadlineDetectionMcdcCoverage`, `DeadlineActionNotifyMonitor`
  - [x] `test_ss_deadline.cpp` — 2 tests: `SsExhaustionTriggersDeadline`, `SsDeadlineMissDuringReplenish`
- [ ] Interrupt Latency Tests
  - [ ] test_irq_latency_histogram.cpp — inject synthetic IRQs, verify ≤ CONFIG_IRQ_LATENCY_MAX_NS
  - [ ] test_nested_irq_latency.cpp — nested ISRs, measure tail-chaining overhead
- [ ] Memory Determinism Tests
  - [ ] test_no_dynamic_alloc_after_init.cpp — scan for PMM/VMM allocations post-init
  - [ ] test_stack_guard_pages.cpp — overflow triggers #PF → hook called
  - [ ] test_static_pool_exhaustion.cpp — exhaust each pool, verify graceful failure
- [ ] Multi-Arch CI
  - [ ] make test-all-hard-rt ARCH=x86_64 — all above tests pass
  - [ ] make test-all-hard-rt ARCH=aarch64 — via Renode (when HAL ready)
  - [ ] make test-all-hard-rt ARCH=riscv64 — via Renode (when HAL ready)

### 0.3.9 — Dynamic RT Task Spawning & I/O Isolation (runelf)
- [ ] **Real-Time `runelf` Extensions:** Extend the `runelf` framework to accept real-time attributes (`--period`, `--wcet`) directly via shell invocation or userspace process spawning.
- [ ] **Admission Control Interception:** Intercept the `runelf` loading sequence and pass execution telemetry parameters directly to the Liu-Leyland implementation (`is_rm_schedulable`). Deny ELF execution if the new task guarantees violate schedulability limits.
- [ ] **Hardware-Enforced WCET Monitoring:** Programmatically bind the parsed execution budget ($C$) to a high-precision hardware timer (HPET/APIC) upon task context activation to guarantee fault-detection containment during runtime overruns.
- [ ] **Sandboxed IPC Routing:** Enforce absolute Ring 3 I/O isolation for the spawned ELF task. Restrict direct MMIO and port I/O access, routing generic hardware or file requests through capability-secured IPC channels backed by core daemons (`vfsd`, `iocd`) under Sporadic Server budgets.
- [ ] **Zero-Overhead Shared Memory Channels:** Implement a capability delegation mechanism to map shared memory pages between the hardware driver server and the `runelf` task. Utilize lock-free SPSC ring buffers spanning page boundaries to achieve zero-syscall, zero-copy real-time data ingestion.
- [ ] **Real-Time `runelf` Extensions & Incremental Loading:**
  - [ ] Implement a chunked ELF parser that maps a maximum of CONFIG_ELF_LOAD_PAGES_PER_SLICE per scheduler invocation
  - [ ] Utilize the existing O(1) MemPool / BufferPool for temporary I/O chunks to block dynamic kernel allocations during the loading process
  - [ ] Bind the loading mechanism to the vfsd Sporadic Server budget to deterministically throttle the I/O load

### 0.3.10 Documentation & Certification Artifacts
## WCET Analysis Report
- [ ] Generate docs/wcet_analysis.md with measured max cycles per kernel function
- [ ] Toolchain: objdump -d + static analysis (aiT, OTAWA, or custom script)
- [ ] docs/safety_manual.md — assumptions, limitations, configuration rules for ASIL D
- [ ] docs/traceability.csv — each requirement (ISO 26262-6) → design → code → test


---

## Phase 5: SMP + Multicore (0.4.x)

### 0.4.1–0.4.2 — APIC & SMP Boot
- [ ] Local/IO APIC, X2APIC, per-CPU GDT/TSS, INIT-SIPI AP startup
- [ ] TPR-based interrupt prioritization, core state isolation

### 0.4.3–0.4.4 — Per-CPU Scheduling & Cache
- [ ] Distributed run queues, real-time load balancer, SYS_SET/GET_AFFINITY
- [ ] Cache coloring allocator, SMP spinlocks/rwlocks, WCET re-audit

### 0.4.5–0.4.6 — TLB Shootdown & IPI Reduction
- [ ] PCID, selective INVPCID, lazy shootdowns, IPI batching, latency profiling

---

## Phase 6: System Integration (0.5.x)

### 0.5.1 — Integration Testing
- [ ] Cross-boundary test suites, 24h stress test (< 0.01% overrun), IPC/context-switch benchmarks

### 0.5.2 — Safety Hardening
- [ ] Syscall determinism docs, pointer isolation, -fstack-protector, release builds, Doxygen

### 0.5.3 — Userspace Library & Toolchain
- [ ] Port or write a deterministic C/C++ standard library (musl adaptation or bespoke freestanding libc) so userspace programs can use `printf`, `malloc`, etc. natively without raw system calls

---

## Phase 7: Safety Systems (0.6.x)

### 0.6.1–0.6.2 — Watchdogs
- [ ] ICH9/HPET hardware watchdog + NMI pre-timeout, PIT fallback, SYS_WATCHDOG_KICK
- [ ] Per-task software watchdog (SYS_WATCHDOG_CREATE), /proc/[pid]/watchdog

### 0.6.3 — Deadlock Detection
- [ ] Wait-for-graph runtime, watchdog-driven detection, forced recovery, SYS_HEALTH_STATUS

### 0.6.4 — Idle-Task Safety Monitors (ASIL D)
- [ ] Idle task: non-destructive RAM March C- algorithm over unused memory regions (back up, write 0x55/0xAA patterns, verify transistor integrity, restore) to detect single-event upsets
- [ ] Idle task: CPU ALU and register verification (mathematical test patterns, MSR integrity validation) to detect latent CPU faults over years of deployment
- [ ] Idle task: precise CPU utilisation tracking (rolling 64-bit execution counter in idle loop; export un-inflated CPU load via /proc/sched), all preemtiable and incrementally. Idle task
Never holds and locks any resources.

---

## Phase 8: Microkernel Transition (0.7.x–0.8.x)

### 0.7.1 — Externalise VFS & Block I/O
- [ ] `vfsd` becomes the exclusive owner of the mount table; remove in-kernel `syscall_path_open` shortcut
- [ ] All filesystem drivers (FAT32, tmpfs) run as separate ring‑3 servers, routed through `vfsd` via IPC
- [ ] Block layer: `atad` user-space driver, block IO exposed via IPC channels (iopl + shared pages)

### 0.7.2 — Externalise Device Drivers
- [ ] Keyboard → `kbd_drv` user-space server; kernel forwards interrupt to driver IPC port
- [ ] Framebuffer → `fb_drv` user-space server
- [ ] Timer/RTC → `timer_drv` user-space server
- [ ] Driver manager daemon: enumerates PCI, spawns driver servers, manages MMIO/capability delegation

### 0.8.1 — Kernel Reduction (µ-kernel core)
- [ ] Kernel retains only: scheduler, IPC primitive (ports/capabilities), page-table management, interrupt routing to user-space
- [ ] Shell, init (PID 1), VFS, all drivers move to ring‑3 server processes
- [ ] Remove kernel shell (created in `kernel.cpp`, runs in ring 0) — replaced by a ring-3 userspace shell ELF (`userspace/shell.c`) loaded by `reboot_from_table()`; requires syscalls for keyboard (`SYS_READ_TERMINAL`), framebuffer (`SYS_WRITE_TERMINAL`), and serial I/O
- [ ] Userspace init spawns: driver manager → device servers → VFS server → shell

### 0.8.2 — Capability-Based Security
- [ ] Each server holds capabilities for owned resources (IO ports, memory regions, IRQ lines)
- [ ] Kernel enforces capabilities on every IPC call; no server can access another server's MMIO region or memory
- [ ] `SYS_CAP_GRANT`/`SYS_CAP_REVOKE` syscalls for capability delegation and revocation

---

## Phase 9: Hardware Drivers & Protocols (0.9.x)

### 0.9.1 — Networking Stack
- [ ] Full TCP/IP stack (ARP, IP, ICMP, UDP, TCP) with Ethernet NIC driver based on 
Already implemented minimize network stack. Implementation strictly running in user space
As network daemon (need).
- [ ] Distributed real-time communication, remote logging, networked control systems

### 0.9.2 — USB Stack
- [ ] USB driver stack (UHCI/EHCI/xHCI) for keyboard, mouse, and storage
- [ ] Replace PS/2 with USB HID for real-hardware input

---

## Technical Debt & Infrastructure (Cross-Cutting)

### Core Data Structures
- [ ] **TaskDef refactor:** Replace positional aggregate init with named/designated initializers or builder pattern. Adding a new parameter currently requires touching every entry in `g_task_defs[]` — easy to silently corrupt field assignment.
- [ ] **Snapshot buffer layout:** Replace manual `sizeof` chain (`off_sched_misc_size()`, `off_daemon_entries()`, etc.) with a struct-based or generator-driven layout. Any change to buffer size shifts all downstream offsets — brittle and hard to audit.

### Test Isolation — Option B: Lazy Daemon Restart via `vfs_touched` Flag

> **Migrated to v0.3.8 — Test & Verification Suite / Infrastructure.**

**Problem:** Every `snapshot_restore()` cycle unconditionally kills and reloads the VFS and IOC daemons via `reload_daemon_tasks()`. This is correct but wasteful: ~95% of tests never touch the VFS (no file open/read/write/stat), yet pay the full daemon death+ELF-load+remount cost (~150k cycles per test). Over 700+ tests this adds significant wall-clock time and creates unnecessary serial log noise ("daemon died" messages that are actually deliberate kills).

**Design (Option B):**

Introduce a per-test `vfs_touched` flag (tracked in the test-runner state) that is set by VFS syscall handlers on actual filesystem operations. `snapshot_restore()` checks this flag:
- **Flag clear (no VFS touched):** Skip `reload_daemon_tasks()` and `reset_and_remount()` entirely. The daemons continue running with their pre-snapshot state intact. `restore_rqpod()` / `rebuild_ready_queue()` still run to fix ready-queue desync. Restoration is O(ready-queue-rebuild) instead of O(daemon-kill + ELF-load + VFS-remount).
- **Flag set (VFS touched):** Full daemon restart as today — daemons are killed, reloaded from ELF, VFS remounted, mounts re-registered.

**Required changes:**

1. **`vfs_touched` counter/flag** (`src/kernel/test/test_isolate.hpp` or `test_runner.hpp`):
   - Declare `extern bool g_vfs_touched` (or counter) in the test framework.
   - Reset to `false` at the start of each `snapshot_restore()` sequence.
   - Expose `void mark_vfs_touched()` for syscall handlers to call.

2. **VFS syscall handlers** (`src/kernel/syscall/syscall_handlers_vfs.cpp`, `syscall_handlers_fs.cpp`, etc.):
   - Insert `mark_vfs_touched()` calls in every syscall that modifies or reads filesystem state: `SYS_OPEN`, `SYS_READ`, `SYS_WRITE`, `SYS_CLOSE`, `SYS_STAT`, `SYS_FSTAT`, `SYS_READDIR`, `SYS_MKDIR`, `SYS_RMDIR`, `SYS_UNLINK`, `SYS_MOUNT`, `SYS_UMOUNT`, `SYS_IOCTL` (if targeting VFS paths), `SYS_MMAP` (if file-backed), `SYS_EXEC` (loads ELF from VFS).
   - Use `#ifdef CONFIG_TEST` guards so the flag is compiled out in release builds (zero overhead in production).

3. **`snapshot_restore()` branching** (`src/kernel/test/test_isolate.cpp`):
   ```cpp
   if (g_vfs_touched) {
       reload_daemon_tasks();
       vfs::reset_and_remount();
   } else {
       // Daemon TCBs are still valid at the restored pool positions.
       // The only risk: their in_ready_queue_/runq links were desynced
       // by cleanup_test_tasks(). Fix that:
       scheduler::rebuild_ready_queue();
   }
   g_vfs_touched = false;
   ```

4. **Daemon crash tests** (`test_vfsd.cpp`, `test_iocd.cpp`):
   - Tests that deliberately crash daemons (`vfsd_crash_restarts`, `vfsd_exhaust_restart_limit`, `iocd_crash_restarts`) must force `g_vfs_touched = true` so that `reload_daemon_tasks()` runs even if VFS was not touched otherwise.

**Safety analysis:**

- **Stale daemon state concern:** If a test touches VFS, the full restart path fires, so no stale state persists. If a test does *not* touch VFS, the daemon state is exactly what it was after the snapshot save (restore_pool_data() rewinds MemPool to snapshot positions, but daemon TCBs were never restored from ELF — their heap-cached FDs, vnodes, and mount table entries remain as they were at snapshot time). This is correct because:
  - The snapshot is taken *after* daemon initialization is complete.
  - Tests that don't touch VFS cannot modify daemon-visible state.
  - The only mutable daemon-visible side effect of a non-VFS test is scheduler state (ready-queue links, which `rebuild_ready_queue()` fixes) and PMM allocations (which `restore_pool_data()` undoes).

- **Daemon "died" log noise:** Fully eliminated for the ~95% case. Only real daemon crashes (page faults, assertion failures) will produce `notify_death()` messages.

**Estimated speedup:** ~70-80% reduction in per-test overhead for the majority of test classes. Full `all` suite runtime expected to drop from ~45s to ~15-20s (direct QEMU, no `-nographic`).

**Verification:**

1. Run each test class individually. Verify that non-VFS classes (selftest, scheduler, spinlock, atomic_context_switch, preemption_under_syscall) show zero "daemon died" messages and pass at the same rate.
2. Run VFS class: must still show 143/143 PASSED with full daemon restart on every test.
3. Run full `all` class: must still show 738/738 PASSED.
4. Compare wall-clock time between baseline (unconditional restart) and Option B.

**Future extension (Option B+):** If IPC state is also expensive to restore, add `ipc_touched` flag with the same pattern. The mechanism generalises to any subsystem whose daemon restart can be deferred.

### Test Isolation — CI/CD Blockers & Infrastructure Fixes

> **Migrated to v0.3.8 — Test & Verification Suite / Infrastructure.**

- [ ] **1. Full `all` suite completion (QEMU watchdog / signal 15):**
  - **Problem:** Running the full `all` test class via direct QEMU invocation gets the QEMU process killed mid-run by signal 15 (SIGTERM) — likely the host tooling watchdog or a timeout mechanism. The suite never completes end-to-end.
  - **Investigation:**
    1. Reproduce with a minimal reproducer: `qemu-system-x86_64 -kernel build/kernel-debug.elf -nographic -no-reboot -m 512M -append "classes=all" -serial file:serial.log`
    2. Check serial.log for the last test name before termination — identify whether tests are still passing or a hang occurs.
    3. Add a test-progress counter (e.g. `printf("[PROGRESS] %d/%d\n", passed, total)` every N tests) to pinpoint when/where QEMU dies.
    4. Rule out host OOM killer (`dmesg | grep oom-kill`) and host CPU throttling.
  - **Fix options:**
    - **Option A:** Extend the host-side timeout (if the watchdog is in the runner script or Makefile) — search for `timeout` commands or `alarm` signals in `Makefile`, `tools/`, and CI scripts.
    - **Option B:** Reduce per-test overhead via Option B (vfs_touched flag) — the suite may complete if each test is faster and total wall time drops below the watchdog threshold.
    - **Option C:** Split `all` into smaller batches (e.g. `all-part1`, `all-part2`) that each complete within the time limit; add a meta-runner script that concatenates results.
  - **Verification:** `make test-qemu classes=all` exits with exit code 0 and prints 727/727 PASSED.

- [ ] **2. `make execute-test` fix (Makefile / expect invocation):**
  - **Problem:** `make execute-test [CLASSES=...]` prints the Makefile help text instead of launching QEMU. The expected `expect`-based runner path is not being reached.
  - **Investigation:**
    1. Trace the Makefile rule for `execute-test`: read `Makefile` and any included `.mk` files to find the conditional that decides whether to run the `expect` script or print help.
    2. Check prerequisites: the rule likely depends on `build/kernel-debug.elf` or similar targets — verify they exist and are up to date.
    3. Test the `expect` script directly: `expect tools/run-test.exp classes=selftest` to isolate the issue from Make.
  - **Fix:** Add the missing prerequisite target or fix the conditional. Alternatively, if `make execute-test` is inherently broken in this env, document the direct-QEMU workaround in `CONTRIBUTING.md` or `AGENTS.md`.
  - **Verification:** `make execute-test CLASSES=selftest` launches QEMU, runs selftest, and prints 132/132 PASSED.

- [ ] **3. Healthcheck.sh pre-flight reliability:**
  - **Problem:** The healthcheck script (`~/jarvis/healthcheck.sh`) must pass (exit 0) before any test automation starts. A failing healthcheck blocks all work. Currently its error output is opaque.
  - **Fix:** Add per-check labels and a summary to healthcheck.sh so a failure pinpoints the exact broken component (missing tool, stale build, etc.). Consider automatic re-build on stale artifact detection.
  - **Verification:** Intentional breakage of each check produces a clear, actionable error message.

### Test Isolation — Documentation Deliverables

- [ ] **4. Daemon restart documentation (test-isolation caveats for task removal/replacement):**
  - **Original goal (abandoned):** Document the memory-subsystem files and the test-isolation architecture, focusing on the MemPool-backed TCB lifecycle during snapshot/restore.
  - **Required deliverable:**
    1. `docs/test_isolation.md` or equivalent — explain:
       - How `restore_pool_data()` rewinds MemPool to snapshot positions, overwriting live daemon TCBs at runtime pool positions.
       - Why snapshot-era TCBs at restored positions have broken page-table links (PMM freed/reallocated intermediate PD/PT pages during test execution).
       - Why killing and recreating via `elf::load()` is the minimal-correctness approach for VFS-touching tests.
       - Why non-VFS tests can skip daemon restart (Option B argument).
       - The `DebugContextSwitchRing` (`TCB::debug_switch_ring[4]`) inspection technique for diagnosing desync bugs.
    2. Cross-reference from `test_isolate.cpp`, `scheduler.cpp`, and `task.cpp` file headers to this doc.
  - **Verification:** A new team member can read the doc and understand why daemons are killed on every snapshot restore, and when it is safe to skip the restart.

- [ ] **5. Memory subsystem architecture doc:**
  - **Original goal (abandoned):** Comprehensively document PMM, VMM, MemPool, BufferPool, and their interactions with test isolation.
  - **Scope:** Single `docs/memory_subsystem.md` covering:
    - PMM: physical page allocation/free, buddy-system layout, `restore_pool_data()` impact.
    - VMM: page-table hierarchy (PML4/PDPT/PD/PT), `map_page_in_pml4`, clone-time sharing gotchas.
    - MemPool: fixed-size slab allocator, position-based snapshot/restore, TCB lifecycle.
    - BufferPool: pre-allocated ring buffers, zero-copy IPC data paths.
    - Integration: how each subsystem reacts to `snapshot_save()` / `snapshot_restore()` — which state is saved, which is rebuilt, and why.
   - **Verification:** A developer debugging a page-fault in a test can find the relevant subsystem description and snapshot/restore interaction within 30 seconds.

### Disabled / Stub / Broken Test Remediation (v0.3.x)

> **Migrated to v0.3.8 — Test & Verification Suite / Infrastructure.**  
> Stalled/starvation-deadlock disabled tests migrated to v0.3.2 and v0.3.3 respectively.

Full scan of all test files identified ~90+ disabled, stub, or broken tests across 15+ files. These must be triaged and addressed per-subsystem.

---

#### Disabled Tests (`#if 0` or registration-commented-out)

| Test | File | Status | Issue |
|------|------|--------|-------|
| `iocd_crash_restarts` | `test_iocd.cpp:121` | `#if 0` | Deliberately kills daemon manually; daemon lifecycle left in non-standard state |
| `vfsd_crash_restarts` | `test_vfsd.cpp:335` | `#if 0` | Same pattern — manual daemon kill breaks snapshot/restore assumptions |
| `vfsd_exhaust_restart_limit` | `test_vfsd.cpp:376` | `#if 0` | Exhausts daemon restart budget; daemon stays permanently dead |
| `MempoolFragmentation` class | `test_resource_exhaustion.cpp:183` | `#if 0` | Pre-existing hang at test 438 (BUGS.md#013) |
| `pci_enumeration_bounded_time` | `test_pci.cpp:274` | `#if 0` | QEMU-dependent performance benchmark, not a correctness test |
| `qemu_debug_exit_success` | `test_debug.cpp:81` | registration commented | Uses `qemu_debug_exit` which kills QEMU — incompatible with batch test runner |
| `qemu_debug_exit_failure` | `test_debug.cpp:82` | registration commented | Same — kills QEMU mid-suite |

- [x] **0.3.x-DISABLED-1:** Fix `iocd_crash_restarts` / `vfsd_crash_restarts` — design a safe daemon-kill mechanism compatible with snapshot/restore (force `g_vfs_touched = true`, allow controlled crash in isolated sub-test). *(Resolved in v0.3.2 — daemon lifecycle fixed; tests re-enabled.)*
- [x] **0.3.x-DISABLED-2:** Investigate and fix `MempoolFragmentation` hang (BUGS.md#013) or document as known limitation. *(Resolved in v0.3.2 — TCB pool capacity bumped, MemPool leaks fixed.)*
- [ ] **0.3.x-DISABLED-3:** Fix `qemu_debug_exit` tests — gate them behind a separate test binary or `CONFIG_TEST_QEMU_EXIT` flag so they run standalone.

---

#### Stub Tests (body is `JARVIS_TEST_PASS()` with no real assertions)

**VFS daemon stubs (9 tests, `test_vfsd.cpp:235`):** `vfsd_handle_read_write`, `vfsd_handle_open`, `vfsd_handle_close`, `vfsd_handle_resolve`, `vfsd_handle_mount_unmount`, `vfsd_handle_stat_fstat`, `vfsd_handle_chdir_getcwd`, `vfsd_invalid_message_type_rejected`, `vfsd_malformed_message_rejected`, `vfsd_unauthorized_task_rejected`, `vfsd_concurrent_requests` — all require post-boot STI for userspace IPC; blocked by test isolation architecture (daemon is killed on snapshot restore).

**IOCD stubs (6 tests, `test_iocd.cpp:56`):** `iocd_keyboard_irq_to_event`, `iocd_serial_irq_to_event`, `iocd_mmio_map_via_capability`, `iocd_mmio_unmap_on_exit`, `iocd_unauthorized_mmio_rejected`, `iocd_multiple_device_handlers`, `iocd_irq_affinity` — IOCD driver infrastructure not yet fully wired in test environment.

**Driver stubs (4 tests, `test_driver.cpp:75`):** `iocd_server_boots`, `keyboard_driver_in_iocd`, `serial_driver_in_iocd`, `driver_server_mmio_via_caps` — no actual assertions; placeholders for driver architecture.

**Capability stubs (21 tests, `test_capability.cpp`):** `capability_create_for_mmio`, `capability_grant_to_task`, `capability_map_mmio`, `capability_revoke`, `capability_cannot_forge`, `iocd_uses_capabilities_for_keyboard`, `cap_create_mmio_valid_bounds`, `cap_create_mmio_invalid_size_zero`, `cap_create_mmio_invalid_phys_addr`, `cap_grant_to_nonexistent_task_fails`, `cap_grant_duplicate_fails`, `cap_map_mmio_success`, `cap_map_mmio_wrong_task_fails`, `cap_map_mmio_duplicate_virt_fails`, `cap_revoke_unmaps`, `cap_revoke_nonexistent_fails`, `cap_forge_random_rejected`, `cap_forge_incremented_rejected`, `cap_transfer_to_child_on_fork`, `cap_inherit_on_exec`, `cap_max_per_task_limit` — entire capability subsystem not implemented; all stubs.

**Buffer pool stubs (2 tests, `test_buffer_pool.cpp`):** `buffer_pool_va_conflict_rejected`, `buffer_pool_zero_va_rejected` — kernel does not yet implement VA conflict detection or VA=0 rejection.

**IPC stubs (2 tests, `test_ipc_extended.cpp`):** `ipc_queue_remove_from_mid`, `ipc_send_sync_timeout` — no API to remove arbitrary sender from middle of blocked list; `send_sync` has no timeout parameter.

**Pipe stubs (2 tests, `test_pipe.cpp`):** `pipe_read_from_empty_nonblock`, `pipe_multiple_writers_no_interleaving` — pipe implementation does not support `O_NONBLOCK`; second test requires multi-task environment.

**Spinlock stub (1 test, `test_spinlock.cpp:235`):** `spinlock_recursive_deadlock_detect` — SpinLock does not implement recursive locking detection; lock() spins forever on recursive entry.

**VMM stub (1 test, `test_vmm.cpp:78`):** `vmm_clone_failure_rollback` — `clone_kernel_pml4` does not implement rollback on OOM; partially allocates page tables on failure.

**IPC benchmark stub (1 test, `test_ipc_benchmark.cpp:143`):** `ipc_bench_recv_self` — measures IPC recv-only latency from pre-filled queue; no actual benchmark harness.

**Syscall stub (1 test, `test_syscall.cpp:430`):** `syscall_signal_sigreturn` — SIGRETURN is stubbed because no signal delivery path exists to populate a user stack with SignalFrame.

- [ ] **0.3.x-STUB-1:** Implement real VFS daemon stubs — requires post-boot STI in test framework or a lightweight IPC mock that bypasses daemon.
- [ ] **0.3.x-STUB-2:** Wire IOCD driver infrastructure in test environment — implement keyboard/serial IRQ-to-event pipeline, MMIO capability mapping.
- [ ] **0.3.x-STUB-3:** Implement capability subsystem (3-4 sprints): cap object model, grant/revoke lifecycle, MMIO mapping integration, fork/exec inheritance.
- [ ] **0.3.x-STUB-4:** Implement buffer pool VA conflict detection and VA=0 rejection.
- [ ] **0.3.x-STUB-5:** Add `ipc_queue_remove` API and `send_sync` timeout parameter.
- [ ] **0.3.x-STUB-6:** Implement `O_NONBLOCK` for pipes and multi-task pipe test infrastructure.
- [ ] **0.3.x-STUB-7:** Implement recursive lock detection in SpinLock or document as known limitation.
- [ ] **0.3.x-STUB-8:** Implement OOM rollback in `clone_kernel_pml4`.
- [ ] **0.3.x-STUB-9:** Implement signal delivery path for `syscall_signal_sigreturn`.

---

#### Entirely Stubbed Test Files (all tests trivial pass)

| File | Tests | Notes |
|------|-------|-------|
| `test_address.cpp` | 6 tests | phys/virt identity mapping, page align, null phys |
| `test_bootparams.cpp` | 4 tests | cmdline parsing |
| `test_gcov.cpp` | 4 tests | gcov handler, instrument bitmask, flush, rdtsc |
| `test_keyboard.cpp` | 5 tests | init, enqueue/dequeue, buffer full, modifiers, self-test |
| `test_multiboot.cpp` | 5 tests | mb2 tag find, framebuffer, memory map, module tags |
| `test_pic.cpp` | 3 tests | remap, mask, EOI |
| `test_serial.cpp` | 4 tests | init baud, putchar, puts crlf, empty string |
| `test_vfs_internal.cpp` | 8 tests | devfs null/zero read, tty resolve, procfs, initrdfs |
| `test_health.cpp` | 5 tests | syscall metrics, fields, counters, privileged, proc |
| `test_shell_redirect.cpp` | 3 tests | redirect to devnull, no target, capture |
| `test_integration.cpp` | 1 test | mandelbrot CRC hash |
| `test_textutils.cpp` | 1 test | core text utilities (TODO) |
| `test_tmpfs_corrupted_metadata.cpp` | 1 test | TODO: implement when low-level VFS hooks available |
| `test_tmpfs_io_timeout.cpp` | 1 test | TODO: implement when allocator instrumentation present |

- [ ] **0.3.x-STUBFILE-1:** Triage each stubbed file — either implement real tests or remove and create tracking issues.
- [ ] **0.3.x-STUBFILE-2:** For `test_tmpfs_*` and `test_textutils` TODOs, add blocker dependencies on the subsystems they require.

---

#### Disabled Starvation/Deadlock Tests (remediated in current work)

> **`DeadlockNestedMutexLoad` → v0.3.2 (pre-requisite), `DeadlockRecoveryResourceReclamation` → v0.3.3.**

| Test | File | Status | Fix |
|------|------|--------|-----|
| `DeadlockNestedMutexLoad` | `test_starvation_deadlock.cpp:231` | Disabled (commented out) | Real `mutex.lock()` with contention on `[](){}` tasks corrupts scheduler state; waiter array overflow at `mutex.cpp:109`. Requires test isolation redesign for blocking primitives. |
| `DeadlockRecoveryResourceReclamation` | `test_starvation_deadlock.cpp:237` | Never implemented | No class definition exists in source. |

---

#### Architecture Stub Registrations (non-x86_64)

`arch/aarch64/test_stubs.cpp` and `arch/riscv64/test_stubs.cpp` contain ~32 stub registration functions that register zero tests on non-x86_64 architectures. These are compile-time placeholders for when ARM64/RISC-V64 HALs are implemented.

- [ ] **0.3.x-ARCHSTUB-1:** No action until ARM64/RISC-V64 HAL work begins (target: 0.3.6). Keep stubs to satisfy linker symbols.

---

#### Pre-Existing Test Failures (unrelated to disabled/stub work)

> **Migrated to v0.3.2 — Pre-Requisite: Bugfix & Stabilisation.** **RESOLVED in v0.3.2.** The 15 pre-existing FAILs in `vfsd`/`iocd`/`buffer_pool` were fixed (commits `e8c19cf`, `99154e4`, `dfc3aec`, and the FAT32 `read_dir_entry` MemPool-leak fix `ab2fe82`). Full suite passes 764/764.

---

#### Tracking

- **Total suite size**: 764 tests (x86_64 debug, CONFIG_DEADLINE_MONITOR_TASK=1) — updated from 754 after WCET/deadline test additions
- **Disabled tests** (compiled out): 7
- **Stub tests** (trivial pass): ~55 individual tests + 1 partially stubbed
- **TODO placeholder tests**: 3
- **Architecture stubs**: ~32 registration functions
- **Genuine failures**: 0 (764/764 PASSED at baseline)
- **Health check**: `bash ~/jarvis/healthcheck.sh` must exit 0 before any fix attempt.

---

### Stashed Work — Audit & Remediation (v0.3.x)

> **Stash drops (`stash@{0}`, `stash@{2}`) → v0.3.2 pre-requisite.** **DROPPED.**  
> **`lib/new.cpp` rewrite (`stash@{3}`) → v0.3.5 (Eliminate operator new/delete).** **Applied as commit `175f301`.**  
> **Error-returning API extraction (`stash@{1}`) → unassigned; keep for selective integration under v0.3.7 (Configuration & Validation) or standalone refactoring sprint.**

Four stashes remain from prior sessions containing partially-complete or alternative-approach work. Each must be reviewed, salvaged if applicable, or discarded.

---

#### `stash@{0}` — Pre-Session WIP (alternative starvation fix approach)

**Location:** `stash@{0}` — `pre-session WIP` on main

**Contents:** Major `kernel.cpp` restructuring (daemon init reordering, boot-stack switch to idle task before `sti()` as alternative starvation fix), RSP corruption DIAG instrumentation in `on_tick()`/`switch_to_task()`, `reload_daemon_tasks()` gutted ("TEMP: skip entirely"), signal handling additions, IPC debug log removal.

**Status:** Alternative approach **superseded** by final `ScopedCurrentTask` + `IrqGuard` pattern (commit `346cb51`). The boot-stack-to-idle switch was a plausible fix but risked masking the root cause. The daemon init reordering and IPC log removal were applied in other forms.

- [x] **0.3.x-STASH-0:** Dropped — superseded by `ScopedCurrentTask` + `IrqGuard` pattern. `reload_daemon_tasks()` gutting never applied.

---

#### `stash@{1}` — Error-Handling Refactor (branch deleted)

**Location:** `stash@{1}` — `WIP on error-handling-refactor`

**Contents:** Error-returning API variants for core subsystems — `PMM::init_err()`, `PMM::alloc_page_err()`, `PMM::alloc_contiguous_err()`, `VMM` error variants, `Mutex`/`Semaphore`/`Queue`/`EventGroup`/`Notify` error-returning wrappers, `VFS` error-returning wrappers (~1040 lines added). **Also deletes all 5 FPU test files** (`test_fpu.cpp`, `test_fpu_clone.cpp`, `test_fpu_multi.cpp`, `test_fpu_sse.cpp`, `test_fpu_xmm_all.cpp`).

**Status:** The error-returning API pattern is architecturally valuable for hard-RT certification (ISO 26262 requires error propagation, not panics). However, the FPU test deletion is destructive — those tests provide coverage for FPU context save/restore in IPC and clone paths. Needs selective extraction.

- [ ] **0.3.x-STASH-1:** Extract error-returning API additions **without** FPU test deletion. Apply to PMM (`alloc_page_err`, etc.), then VMM, then sync primitives. Full `make test-all-debug` validation after each subsystem.
- [ ] **0.3.x-STASH-1b:** Evaluate whether the new sync primitives (`EventGroup`, `Notify`) duplicate existing IPC primitives or fill a real gap. If valuable, add separately with tests.

---

#### `stash@{2}` — Our-Fixes (minor bugfix audit)

**Location:** `stash@{2}` — `On main: our-fixes`

**Contents:** Semaphore `memset` cleanup already applied. Pipe init field-by-field fix already applied. `test_task_lifecycle` and `test_waitpid` use-after-free changes — further inspection needed.

**Status:** The semaphore and pipe fixes are already in HEAD. The use-after-free "fixes" in `test_task_lifecycle` and `test_waitpid` actually *introduce* use-after-free by accessing `child1->id` after `delete child1`. **Do not apply.**

- [x] **0.3.x-STASH-2:** Dropped — semaphore/pipe fixes already in HEAD; the use-after-free "fixes" were correctly NOT applied (test files capture ID before delete).

---

#### `stash@{3}` — WIP on Starvation/Deadlock (ScopeGuard + new.cpp)

**Location:** `stash@{3}` — `WIP on main: a9bf8a2`

**Contents:** ScopeGuard cleanup pattern (already applied in final fix), MemPool `ready_`/`contains()`/`is_ready()` (already applied), test RAII fixtures (already applied), `test_vfs_fat32` fixture (already applied), `test_buffer_pool` transfer fix (already applied). **Not applied: `lib/new.cpp` rewrite.**

**`lib/new.cpp` latent bug:** Current `operator new` falls back to PMM via bump-heap overflow path, but `operator delete` calls `MemPool::free()` unconditionally. If a PMM-backed allocation (too large for MemPool) is deleted, it passes a non-MemPool pointer to `MemPool::free()`, causing corruption. The stash adds:
- `PmmAllocHdr` header before each PMM allocation tracking page count
- `MemPool::contains()` check in `operator delete` — if pointer is not in any pool, calls `pmm_free()` which frees the PMM pages
- Proper PMM page freeing instead of MemPool free

- [x] **0.3.x-STASH-3:** Applied `lib/new.cpp` rewrite (commit `175f301`) — fixes latent heap corruption (PMM-backed allocations deleted via `MemPool::free`). Validated via full test suite.
- [x] **0.3.x-STASH-3b:** `operator delete` handles three cases: (a) MemPool-allocated → `MemPool::free`, (b) PMM-backed (bump overflow) → page-by-page PMM free via `PmmAllocHdr`, (c) `nullptr` → no-op.
