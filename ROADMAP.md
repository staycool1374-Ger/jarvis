# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 3 SYSTEM SERVICES MODE
**Status:** ACTIVE — System Services.
**Target Focus:** v0.2.21 — Kernel Configuration & Portability: jarvis_config.h, check-config, syscall gating, multi-arch HAL.

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

## Phase 3: System Services & Hardware (v0.12.14–v0.2.22)

### 0.2.21 — Kernel Configuration & Portability (Detailed Step-by-Step Tasks)
- [x] Create jarvis_config.h Central Configuration Header
  - [x] Create src/kernel/jarvis_config.h with #ifndef JARVIS_CONFIG_H / #define / #endif guard
  - [x] Add CONFIG_VERSION macro with value "0.2.21" for compile-time version detection
  - [x] Add Doxygen-style comments for every #define explaining effect, valid range, and default
  - [x] Include fallback defaults for every config so kernel builds without user-provided config
  - [x] Add architecture detection: #if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv64)
  - [x] Define CONFIG_ARCH_X86_64, CONFIG_ARCH_AARCH64, CONFIG_ARCH_RISCV64 mutually exclusive based on ARCH Makefile variable
- [x] Migrate Scheduling Tunables
  - [x] CONFIG_MAX_TASKS (default 64) — replace MAX_TASKS in scheduler.hpp and task.hpp
  - [x] CONFIG_TICK_HZ (default 1000) — replace BootParams::timer_hz default; update arch::Timer::init()
  - [x] CONFIG_PRIORITY_CEILING (default 255) — replace BootParams::scheduler_priority_ceiling
  - [x] CONFIG_PREEMPTION (default 1) — replace BootParams::preempt_enabled; gate Scheduler::preempt() calls
  - [x] CONFIG_IDLE_YIELD (default 0) — add yield in idle loop when enabled
  - [x] CONFIG_TIME_SLICING (default 1) — add round-robin time-slice config per priority
- [x] Migrate Memory Layout Tunables (Architecture-Overridable)
  - [x] CONFIG_PAGE_SIZE (default 4096) — replace all 3× PAGE_SIZE definitions; validate power-of-2
  - [x] CONFIG_HHDM_OFFSET (default 0xFFFF800000000000 x86_64, arch-specific for ARM/RISC-V) — replace arch::HHDM_OFFSET
  - [x] CONFIG_PML4_USER_COUNT (default 256) — x86_64 only; page table user entries
  - [x] CONFIG_USER_SPACE_LIMIT (default 0x00007FFFFFFFFFFF) — user virtual address ceiling
  - [x] CONFIG_STACK_SIZE (default 65536 / 64 KiB) — replace both STACK_SIZE definitions; validate ≥ CONFIG_MIN_STACK_SIZE
  - [x] CONFIG_HEAP_SIZE (default 16 MiB) — replace HEAP_SIZE in constants.hpp
  - [x] CONFIG_MIN_STACK_SIZE (default 4096) — minimum stack for task creation safety check
- [x] Migrate Subsystem Sizing Constants
  - [x] CONFIG_MAX_FDS (default 32) — replace MAX_FDS in vfs.hpp
  - [x] CONFIG_MAX_MOUNTS (default 32) — replace MAX_MOUNTS in vfs.hpp
  - [x] CONFIG_MAX_DRIVERS (default 16) — replace MAX_DRIVERS in driver_manager.hpp
  - [x] CONFIG_MAX_DAEMONS (default 16) — replace MAX_DAEMONS in daemon_mgr.hpp
  - [x] CONFIG_MAX_PROGRAMS (default 32) — replace MAX_PROGRAMS in program.hpp
  - [x] CONFIG_IPC_MAX_MSG_SIZE (default 64) — replace MAX_MSG_SIZE in ipc.hpp
  - [x] CONFIG_IPC_MAX_QUEUE_MSG (default 16) — replace MAX_QUEUE_MSG in ipc.hpp
  - [x] CONFIG_IPC_PRIORITY_LEVELS (default 32) — replace PRIORITY_LEVELS in ipc.hpp
  - [x] CONFIG_IPC_SHMEM_MAX_PAGES - maximum of shared memory pages for ipc between via runlet loaded user space program in user task and device driver
  - [x] CONFIG_MAX_PROCESS_PAGES - maximum of physical pages via runelf loaded user space program in user task, for static reservation
  - [x] CONFIG_MAX_SIGNAL_HANDLERS (default 32) — replace MAX_SIGNAL_HANDLERS in signal.hpp
  - [x] CONFIG_VFS_MAX_PATH (default 256) — replace MAX_PATH in vfs.hpp
  - [x] CONFIG_TASK_NAME_LEN (default 16) — add task name length limit in task.hpp
- [x] Migrate MemPool Configuration
  - [x] CONFIG_MEMPOOL_NUM_POOLS (default 9) — number of fixed-size pools
  - [x] CONFIG_MEMPOOL_BLOCK_SIZES — comma-separated list: 16,32,64,128,256,512,1024,2048,4096
  - [x] CONFIG_MEMPOOL_BLOCK_COUNTS — comma-separated list matching pool count: 256,128,64,32,16,8,4,2,1
  - [ ] CONFIG_MEMPOOL_TOTAL_SIZE — computed sum (validated at check-config time)
  - [ ] Refactor MemPool::pool_table to use config arrays instead of hardcoded values
- [x] Implement INCLUDE_ Syscall Gating — CONFIG_INCLUDE_SYS_* defines in jarvis_config.h
- [x] Architecture Feature Detection Flags
  - [ ] CONFIG_HAS_FPU (default 1 on x86_64, 0 on ARM/RISC-V until implemented) — gate FXSAVE/FXRSTOR
  - [ ] CONFIG_HAS_RDRAND (default 1 on x86_64) — gate hardware RNG usage
  - [ ] CONFIG_HAS_MPU (default 0) — future memory protection unit support
  - [ ] CONFIG_HAS_HPET (default 0) — gate HPET driver (v0.3.1)
  - [ ] CONFIG_HAS_APIC (default 1 on x86_64) — gate APIC timer vs PIT
  - [ ] CONFIG_HAS_GIC (default 1 on aarch64) — ARM interrupt controller
  - [ ] CONFIG_HAS_PLIC (default 1 on riscv64) — RISC-V interrupt controller
  - [x] CONFIG_HAS_SBI (default 1 on riscv64) — SBI runtime services
- [x] Hook Configuration Points
  - [x] CONFIG_IDLE_HOOK (default 0) — function pointer void (*idle_hook)(void) called each idle iteration
  - [x] CONFIG_TICK_HOOK (default 0) — function pointer void (*tick_hook)(uint64_t ticks) called on timer tick
  - [x] CONFIG_STACK_OVERFLOW_HOOK (default 0) — void (*stack_overflow_hook)(TaskControlBlock*) on detected overflow
  - [x] CONFIG_OOM_HOOK (default 0) — void (*oom_hook)(size_t requested_size) on allocation failure
  - [x] CONFIG_INIT_HOOK (default 0) — void (*init_hook)(void) after daemon initialization complete
  - [x] Declare weak symbols in jarvis_config.h so user can override without modifying kernel
- [x] Custom Assertion Macro
  - [x] CONFIG_ASSERT(expr) — replace JARVIS_ASSERT macro in assert.hpp
  - [x] Default: #define CONFIG_ASSERT(x) do { if(!(x)) panic(#x, __FILE__, __LINE__); } while(0)
  - [x] Allow user to define custom CONFIG_ASSERT before including jarvis_config.h
  - [ ] Update all JARVIS_ASSERT usages to CONFIG_ASSERT (future refactor)
- [x] Consolidate Duplicate Constants
  - [x] Search codebase for all PAGE_SIZE definitions — consolidate to CONFIG_PAGE_SIZE
  - [x] Search for all STACK_SIZE definitions — consolidate to CONFIG_STACK_SIZE
  - [x] Update constants.hpp to #include "jarvis_config.h" and use config values
  - [x] Update arch-specific constants (arch/x86_64/hal/, arch/aarch64/, arch/riscv64/) to reference config
- [x] Extend Makefile with check-config Target
  - [x] Add check-config phony target that runs validation script
  - [x] Create tools/check-config.py (or shell script) that:
  - [ ] Parses jarvis_config.h for all CONFIG_* defines
  - [ ] Validates: power-of-2 for sizes, range checks (e.g., CONFIG_MAX_TASKS >= 2 && <= 4096)
  - [ ] Checks dependencies: CONFIG_MEMPOOL_NUM_POOLS == count of sizes == count of counts
  - [ ] Validates arch-specific constraints (e.g., CONFIG_HHDM_OFFSET page-aligned)
  - [ ] Ensures CONFIG_TICK_HZ divides CONFIG_TIMER_CLOCK_HZ evenly (PIT: 1193182)
  - [ ] Verifies CONFIG_STACK_SIZE >= CONFIG_MIN_STACK_SIZE
  - [ ] Checks CONFIG_HEAP_SIZE ≥ CONFIG_MEMPOOL_TOTAL_SIZE
  - [x] Reports errors/warnings with line numbers
  - [x] Make check-config run automatically during make all (optional, controlled by ENFORCE_CONFIG_CHECK=1)
  - [x] Add make config-summary target to print all active config values
- [ ] Integration Testing & Documentation
  - [x] Test build with default config: make ARCH=x86_64
  - [ ] Test build with minimal config (disable most syscalls, small pools): verify size reduction
  - [ ] Test build with custom config via make CONFIG_FILE=my_config.h
  - [x] Run make check-config and verify all validations pass
  - [x] Run make test-all-debug — ensure 680/680 tests still pass
  - [ ] Update README.md with configuration guide and jarvis_config.h reference
  - [ ] Document migration path for existing BootParams runtime overrides (still supported, config provides compile-time defaults)

### 0.2.22 — ARM & RISC-V Portability

##Builds on the v0.2.20 `jarvis_config.h` HAL to make Jarvis compile and boot on ARM Cortex-A and RISC-V (RV64) hardware. Every architecture-dependent surface (page tables, interrupts, context switch, timer, boot) gets an `arch/<isa>/` implementation selected by the build system.
- [ ] **Architecture HAL refactor** — lift all `src/kernel/arch/` code behind `CONFIG_ARCH`-selected directories (`arch/x86_64/`, `arch/aarch64/`, `arch/riscv64/`). Each arch exports the same function signatures: `read_cr3()`/`write_cr3()`, `irq_enable()`/`irq_disable()`, `halt()`, `inb()`/`outb()`, `invlpg()`, `fxsave()`/`fxrstor()`, `rdtsc()`.
- [ ] **ARM Cortex-A (aarch64)**
  - [ ] Boot: multi-core entry (spin-table / PSCI), MMU init with TCR/MAIR, page table walk (4-level TTBR0_EL1/TTBR1_EL1)
  - [ ] Interrupts: generic timer (CNTP_CTL_EL0), GICv2/v3 distributor & CPU interface, DAIF masking
  - [ ] Context switch: kernel-mode SVC stack, EL1↔EL0 transitions (ERET), FPU via FPEXC32_EL2 / VFP
  - [ ] Timer: generic timer compare (CNTP_CVAL_EL0), 1–100 MHz configurable tick rate
  - [ ] Minimal QEMU virt platform support (PL011 UART, virtio-mmio transport)
- [ ] **RISC-V (RV64)**
  - [ ] Boot: SBI init, mstatus/satp setup, page table walk (Sv39), single-IPI for multi-core
  - [ ] Interrupts: CLINT (mtime/mtimecmp) or ACLINT, PLIC, mstatus.MIE/mip delegation
  - [ ] Context switch: S-mode scratch CSR, U-mode↔S-mode transitions (SRET), FPU via mstatus.FS
  - [ ] Timer: CLINT mtimecmp-based, SBI set_timer extension as fallback
  - [ ] Minimal QEMU virt platform support (NS16550 UART, virtio-mmio transport)
- [ ] **Cross-architecture common**
  - [ ] Build system: `ARCH=x86_64|aarch64|riscv64` selects toolchain prefix, linker script, and objects
  - [ ] Linker scripts per arch (page-aligned `.text`/`.rodata`/`.data`/`.bss`, HHDM region reservation)
  - [ ] Architecture-agnostic `lib/` (string, ring buffer, ChaCha20, test framework) — no changes needed
- [ ] Cross-arch atomics: Leverage C++20 std::atomic and std::atomic_ref with explicit memory_order instead of legacy compiler builtins across all three ISAs
  - [ ] UART driver abstraction: arch exports `uart_putc()`/`uart_getc()`, kernel `Logger` uses it uniformly

---

## Phase 4: Hard Real-Time (0.3.x)
##v0.3.x — Hard Real-Time Compliance Redesign (Detailed Step-by-Step Tasks)
Executive Summary
Current state: Soft real-time with rate-monotonic scheduling but unbounded WCET, no priority inheritance on mutex/semaphore/queue, PIC-based interrupt controller with unbounded ISR latency, dynamic PMM/VMM allocation paths, and sporadic server only for daemons. Target: Hard real-time per ISO 26262 ASIL D / IEC 61508 SIL 4.

Notes
- All changes must pass make test-all-debug (675/675) before each release
- ResourceTracker must show zero leaks in all hard-RT tests
- Renode simulation for ARM64/RISC-V64 required before M3
- CONFIG_HARD_REAL_TIME=0 builds must remain functionally identical to v0.2.21 (Soft-RT compatibility)

### 0.3.1 — Deterministic Scheduling
- [ ] Determinism & Bounded WCET (Pillar 1)
  - [ ] Scheduler — Replace O(n) Scan with O(1) Priority Bitmap
  - [ ] Add CONFIG_MAX_PRIORITY (default 256) to jarvis_config.h
  - [ ] Implement PriorityBitmap class (256-bit, 4×uint64_t) with __builtin_ctzll/__builtin_clzll for O(1) highest-ready lookup
  - [ ] Replace Scheduler::next_task() linear scan with bitmap search
  - [ ] Add per-priority ready queues (array of TaskControlBlock* head/tail)
  - [ ] Update add_task/remove_task/on_tick to maintain bitmap + queues
  - [ ] Validate: measure next_task() cycles — must be ≤ 150 cycles on x86_64
- [ ] Sporadic Server — Extend to All Hard Real-Time Tasks
  - [ ] Add CONFIG_SPORADIC_SERVER_MAX_TASKS (default 8) to config
  - [ ] Make SporadicServer allocatable per-task via TaskControlBlock::init_sporadic_server()
  - [ ] Add SporadicServer::deadline_miss_handler callback (weak symbol) for Pillar 2
  - [ ] Add CONFIG_SPORADIC_SERVER_BUDGET_GRANULARITY (ticks per budget unit)
- [ ] Eliminate Unbounded Loops in Hot Paths
  - [ ] Scheduler::reap_orphans() — replace while (reaped_any) with single-pass + deferred work queue
  - [ ] Scheduler::cleanup_zombies() — bound iteration to CONFIG_MAX_TASKS
  - [ ] MemPool::alloc() — verify O(1) free-list traversal (no bitmap scan)
  - [ ] VMM::map_page() — verify page-walk depth bounded (4 levels fixed)
  - [ ] Audit all for loops in scheduler.cpp, task.cpp, mempool.cpp, vmm.cpp — add CONFIG_*_MAX_ITERATIONS bounds


### 0.3.2 Strict Deadline Adherence — Zero-Tolerance (Pillar 2)
- [ ] Deadline Miss Detection & Handler
  - [ ] Add TaskControlBlock::deadline_ticks (absolute deadline) and deadline_missed flag
  - [ ] In Scheduler::on_tick(): check current_tick > task->deadline_ticks for RUNNING/READY tasks
  - [ ] On miss: call weak void deadline_miss_handler(TaskControlBlock*, uint64_t missed_by_ticks)
  - [ ] Default handler: log + panic in debug; in release: CONFIG_DEADLINE_ACTION enum { PANIC, KILL_TASK, DEMOTE_PRIORITY, NOTIFY_MONITOR }
  - [ ] Add CONFIG_DEADLINE_MONITOR_TASK — dedicated watchdog task (highest priority) that scans for misses
- [ ] Budget Enforcement with Hard Preemption
  - [ ] SporadicServer::consume() — when budget hits 0, immediately call Scheduler::reschedule() (not deferred)
  - [ ] Add CONFIG_HARD_BUDGET_PREEMPTION — force context switch on budget exhaustion
  - [ ] Integrate with SporadicServer::current_priority() — exhausted tasks run at bg_priority (configurable per server)
- [ ] Time-Partitioning (ARINC 653 Style)
  - [ ] Add TimePartition struct: start_tick, duration_ticks, budget_ticks, task_set[]
  - [ ] Scheduler::partition_schedule() — major frame timer, minor frame dispatcher
  - [ ] CONFIG_TIME_PARTITIONING — enable static schedule table (compile-time generated from config)
  - [ ] Overrun detection: partition budget exhausted → deadline_miss_handler + partition switch

### 0.3.3 — Inheritance & Ceiling
## Preemptive Priority-Based Scheduling + Priority Inversion Mitigation (Pillar 3)
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
- [ ] Deadline Miss Tests
  - [ ] test_deadline_miss_detection.cpp — task misses deadline, handler invoked
  - [ ] test_deadline_action_kill.cpp — CONFIG_DEADLINE_ACTION=KILL_TASK verified
  - [ ] test_deadline_monitor.cpp — watchdog task detects miss within 1 tick
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
