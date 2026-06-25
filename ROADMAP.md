# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 3 SYSTEM SERVICES MODE
**Status:** ACTIVE — System Services.
**Target Focus:** v0.2.22 — aarch64 Port: HAL refactoring, ARM Cortex-A boot, context switch, timer, UART, Makefile cross-arch cleanup.

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

## Phase 3: System Services & Hardware (v0.12.14–v0.2.24)

### 0.2.22 — aarch64 Port (ARM Cortex-A)

Builds on `jarvis_config.h` (v0.2.21) to bring Jarvis up on ARM Cortex-A in QEMU virt. Every architecture-dependent surface (page tables, interrupts, context switch, timer, boot) gets an `arch/aarch64/` implementation.

- [ ] **A. HAL Interface Refactoring (structural)**
  - [ ] Move `arch/x86_64/timer.cpp`, `gdt.cpp`, `idt.cpp` → `arch/x86_64/hal/` with interface headers matching `arch/hal/` API
  - [ ] `arch/hal/context.hpp` — make `ArchContext` arch-selected (x86_64 vs aarch64 vs riscv64)
  - [x] `arch/hal/io.hpp` — add `#elif CONFIG_ARCH_AARCH64` branch mapping port I/O to MMIO (`arch/aarch64/hal/io_impl.hpp`)
  - [x] `arch/hal/page_table.hpp` — dispatches to arch-specific `page_table_impl.hpp` per arch
  - [x] Build system: arch-specific `OBJ` lists in `mk/rules.mk` via `arch/$(ARCH)/` source discovery
  - [x] Validate `linker_aarch64.ld` — links successfully, sections/symbols verified via `make build/kernel-debug.elf ARCH=aarch64`

- [x] **B. Boot Entry (`arch/aarch64/boot.S`)**
  - [x] EL2→EL1 transition (QEMU virt starts at EL2), or stay at EL1 if configured
  - [x] Exception level drop, VBAR_EL1 vector table install
  - [ ] MMU init: TCR_EL1 (4KB granule, 4-level), MAIR_EL1 (normal/device memory), TTBR0_EL1/TTBR1_EL1 with 4-level page tables
  - [ ] Identity-map kernel low region + map higher half using boot page tables
  - [ ] Enable MMU (SCTLR_EL1.M), jump to higher half
  - [x] Call `higherhalf_entry(uint64_t magic, uint64_t dtb_ptr)` — device tree pointer instead of multiboot

- [x] **C. Page Tables (`arch/aarch64/hal/page_table_impl.hpp`)**
  - [x] `ArchPageTable` class: `current()`, `activate(phys)`, `tlb_flush(va)`, `tlb_flush_all()`
  - [x] 4-level page table walk (L0–L3, 9-bit each, 4KB granule), `map_page()`, `unmap_page()`, `get_physical()`
  - [ ] Support 2MB block mappings at L2 (huge pages)

- [x] **D. Context Switch (`arch/aarch64/hal/context.hpp`)**
  - [x] `ArchContext` struct: x0–x29, x30/LR, SP_EL1, ELR_EL1, SPSR_EL1
  - [x] `ArchContextManager::init_stack()` — build initial pt_regs frame for ERET to EL0
  - [x] `ArchContextManager::switch_to(from, to, rsp)` — save/restore callee-saved regs, SP_EL1, ELR_EL1
  - [x] `switch_to_task()` assembly trampoline — context-switch via ERET

- [x] **E. Interrupts & Generic Timer**
  - [x] DAIF masking: `irq_enable()`/`irq_disable()` via `MSR DAIFClr/DAIFSet, #2`
  - [x] GICv3: distributor init, CPU interface init, SPI/PPI routing, eoi
  - [x] `ArchInterruptController::init()`, `eoi()`, `mask()`, `unmask()`
  - [x] VBAR_EL1 exception vector table (~32 entries): sync, IRQ, FIQ, SError × 4 exception levels
  - [x] SVC #0 handler for syscall entry (EL0→EL1)
  - [x] `arch::Timer`: init via CNTP_TVAL_EL0, ticks via ticks_ counter, ns via CNTPCT_EL0
  - [x] `Timer::set_frequency()` — program CNTP_TVAL_EL0 period, no calibration needed (CNTFRQ_EL0 is fixed)

- [x] **F. UART & Serial**
  - [x] `arch::Serial` — PL011 UART at `0x9000000` (QEMU virt): init (8N1), putc, getc
  - [x] Wire into kernel `Logger::init()` and `debug_write()` via `uart_putc()`

- [ ] **G. PCI (ECAM)**
  - [ ] Replace CF8/CFC port I/O with ECAM memory-mapped config space at QEMU virt ECAM base — stubs exist (`pci.cpp`/`virtio.cpp`), full ECAM driver pending
  - [ ] PCI bus scan, BAR parsing, MSI/MSI-X capability detection

- [x] **H. Remaining HAL surface**
  - [x] `arch::RTC` — ARM Generic Timer based (CNTPCT_EL0 / CNTFRQ_EL0)
  - [x] `arch::cpuid()` — read ID_AA64*_EL1 system registers for FPU/SIMD feature detection (`arch/aarch64/hal/cpuid_impl.hpp`)
  - [x] `arch::IrqGuard` — RAII via DAIF masking
  - [x] `arch::rdrand64()` — via `arch/aarch64/hal/rand_impl.hpp` (RNDRRS_EL0 or ChaCha20 fallback)
  - [ ] `arch::Keyboard` — stub (no PS/2 on ARM virt); future: virtio-input

- [x] **I. Integration & Tests**
  - [x] `make run ARCH=aarch64` — boots to kernel UART output (debug builds + safe-class tests)
  - [x] Validate Renode platform `tools/renode/jarvis-aarch64.repl`
  - [x] Port kernel selftest framework to aarch64 (test registration + serial output) — safe class runs
  - [ ] `make test-all-debug ARCH=aarch64` — all test classes pass (safe class OK, full suite has failures)
  - [x] x86_64 must remain fully functional through every change


### 0.2.23 — riscv64 Port (RV64)

Follows the same pattern established by v0.2.22, targeting RISC-V 64-bit (RV64) in QEMU virt.

- [ ] **A. Boot Entry (`arch/riscv64/boot.S`)**
  - [ ] M-mode→S-mode transition via SBI, or start in S-mode (QEMU virt `-bios default`)
  - [ ] SATP init with Sv39 page tables (3-level, 4KB pages, 512 GiB virtual address space)
  - [ ] Trap vector setup (stvec), S-mode CSRs (sie, sip, sstatus)
  - [ ] Identity-map kernel + map higher half, enable MMU, jump to higher half
  - [ ] Call `higherhalf_entry(uint64_t magic, uint64_t dtb_ptr)`

- [ ] **B. Page Tables (`arch/riscv64/hal/page_table_impl.hpp`)**
  - [ ] `ArchPageTable` class with SATP CSR: `current()`, `activate(phys)`, `tlb_flush(va)`, `tlb_flush_all()`
  - [ ] Sv39: 3-level page table walk (9-bit each, 4KB pages), `map_page()`, `unmap_page()`
  - [ ] Support 2MB and 1GB huge pages (page table entry R/W bits)

- [ ] **C. Context Switch (`arch/riscv64/hal/context.hpp`)**
  - [ ] `ArchContext` struct: ra, sp, gp, tp, s0–s11, sepc, sstatus, stvec
  - [ ] `ArchContextManager::init_stack()` — build trap frame for SRET to U-mode
  - [ ] `ArchContextManager::switch_to()` — save/restore callee-saved, SRET
  - [ ] S-mode scratch CSR for kernel stack pointer on trap entry

- [ ] **D. Interrupts (PLIC) & CLINT Timer**
  - [ ] S-mode interrupt delegation: sie.SEIE (external), sie.STIE (timer), sie.SSIE (software)
  - [ ] PLIC: init, enable/disable IRQ per context, priority, claim/complete
  - [ ] `ArchInterruptController::init()`, `eoi()`, `mask()`, `unmask()`
  - [ ] `arch::Timer` via CLINT mtimecmp (MMIO) or SBI set_timer ecall
  - [ ] `Timer::init()`, `ticks()`, `ns()`, `handle_irq()`, `set_frequency()`
  - [ ] ECALL handler for syscall entry (U-mode→S-mode)

- [ ] **E. UART & Serial**
  - [ ] `arch::Serial` — NS16550A UART at `0x10000000` (QEMU virt): init, putc, getc
  - [ ] Wire into kernel `Logger`

- [ ] **F. RNG**
  - [ ] No native RNG in RISC-V ISA. Implement via SBI getrandom extension or ChaCha20 PRNG fallback

- [ ] **G. PCI (ECAM)**
  - [ ] Same ECAM mechanism as aarch64 — memory-mapped config space

- [ ] **H. Remaining HAL surface**
  - [ ] `arch::RTC` — via mtime/mtimecmp
  - [ ] `arch::cpuid()` — read misa, mvendorid, marchid CSRs
  - [ ] `arch::IrqGuard` — RAII via sstatus.SIE
  - [ ] Keyboard stub (virtio-input later)

- [ ] **I. Integration & Tests**
  - [ ] `make run ARCH=riscv64` — boots to UART output in QEMU
  - [ ] Validate Renode platform `tools/renode/jarvis-riscv64.repl`
  - [ ] Initial test class passes on riscv64
  - [ ] x86_64 and aarch64 remain fully functional

### 0.2.24 — Cross-Architecture Hardening

- [ ] **Cross-arch atomics** — replace `__sync_*` builtins with `std::atomic` / `std::atomic_ref` using explicit memory_order across all three ISAs
- [ ] **Boot flow unification** — generalize `higherhalf_entry()` to accept both multiboot (x86_64) and device tree (aarch64, riscv64) boot info
- [ ] **UART driver abstraction** — `arch/hal/serial.hpp` defines `uart_putc()`/`uart_getc()`, `Logger` uses it uniformly
- [ ] **Renode CI** — `make renode-test ARCH=aarch64` and `ARCH=riscv64` as CI gate
- [ ] **Virtio transport unification** — abstract MMIO vs PCI transport for virtio devices
- [ ] **Memory model** — verify C++20 `std::atomic` works correctly across all three ISAs (no SEQCST assumptions)
- [ ] **Cross-arch test suite** — common test classes that validate identical behavior on all three architectures

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
