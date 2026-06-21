# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 3 SYSTEM SERVICES MODE
**Status:** ACTIVE — System Services.
**Target Focus:** v0.2.18 — Observability & Portability: Kernel log ring buffer (SYS_KLOG, dmesg), HAL abstraction, arch/x86_64/ migration, multi-arch build (ARCH variable), secure exec (CheckedPointer), regression audit, PCI bus enumeration / device tree debug output.

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

### 0.2.18 — Observability & Portability
- [ ] Kernel log ring buffer (SYS_KLOG, dmesg), HAL abstraction, arch/x86_64/ migration
- [ ] Multi-arch build (ARCH variable), secure exec (CheckedPointer), regression audit
- [ ] PCI bus enumeration / device tree debug output (pci_print_tree, sysfs /proc/pci)

### 0.2.19 — Kernel Memory Safety
- [ ] Audit existing `new`/`delete` usages in kernel code for consistency with the RAII pattern

### 0.2.19 — System Calls & Storage
- [ ] SYS_YIELD — cooperative task yielding for CPU-bound tasks
- [ ] SYS_REBOOT / SYS_HALT — system power management from userspace
- [ ] AHCI/SATA driver with NCQ (replaces ATA PIO for bare-metal storage)
- [ ] DMA completion interrupt infrastructure (ISR acknowledges and fires for storage I/O)
- [ ] Double-buffered DMA transfer support (ping-pong buffers for streaming storage)

### 0.2.20 — Kernel Configuration & Portability

A FreeRTOS-inspired `jarvis_config.h` header that makes Jarvis compile-time customizable and architecture-portable. All tunables currently scattered as `constexpr` in 20+ files get a single configuration home.

- [ ] **`jarvis_config.h`** — central configuration header with `#ifndef` guard and sensible defaults. Every `#define` has a Doxygen comment explaining its effect and valid range.

- [ ] **Migrate scheduling tunables:** `CONFIG_MAX_TASKS` (was `MAX_TASKS=64`), `CONFIG_TICK_HZ` (was `BootParams::timer_hz=1000` via bootargs now becomes compile-time default), `CONFIG_PRIORITY_CEILING` (was `255`), `CONFIG_PREEMPTION` (was `preempt_enabled=true`)

- [ ] **Migrate memory layout tunables:** All `arch::` constants (`HHDM_OFFSET`, `PML4_USER_COUNT`, `PAGE_SIZE`, `USER_SPACE_LIMIT`, `STACK_SIZE`, `HEAP_SIZE`) — these are the main portability barriers; make them arch-overridable via `CONFIG_*`.

- [ ] **Migrate subsystem sizing:** `CONFIG_MAX_FDS`, `CONFIG_MAX_MOUNTS`, `CONFIG_MAX_DRIVERS`, `CONFIG_MAX_DAEMONS`, `CONFIG_MAX_PROGRAMS`, `CONFIG_IPC_MAX_MSG_SIZE`, `CONFIG_IPC_MAX_QUEUE_MSG`, `CONFIG_IPC_PRIORITY_LEVELS`, `CONFIG_MAX_SIGNAL_HANDLERS`, `CONFIG_VFS_MAX_PATH` — all these subsystem caps currently hardcoded in individual headers.

- [ ] **Migrate MemPool sizing:** Replace inline pool-table array with `CONFIG_MEMPOOL_*` defines (number of pools, block sizes, block counts per pool).

- [ ] **`INCLUDE_` syscall gating** — FreeRTOS-style function inclusion/exclusion. Each syscall gets a `CONFIG_INCLUDE_SYS_<NAME>` toggle so unused syscalls can be stripped at compile time for code size reduction (safety-critical / small-footprint builds).

- [ ] **Architecture feature detection:** `CONFIG_HAS_FPU`, `CONFIG_HAS_RDRAND`, `CONFIG_HAS_MPU`, `CONFIG_HAS_HPET` — compile-time feature flags for conditional compilation of architecture-dependent code paths.

- [ ] **Hook configuration:** `CONFIG_IDLE_HOOK`, `CONFIG_TICK_HOOK`, `CONFIG_STACK_OVERFLOW_HOOK`, `CONFIG_OOM_HOOK` — hook points for application-specific callbacks at idle, tick, stack overflow, and OOM events.

- [ ] **`CONFIG_ASSERT`** — custom assertion macro that replaces the current hardcoded `JARVIS_ASSERT` in test framework (allows user-defined panic/error action).

- [ ] **Consolidate duplicate constants:** Merge the 3× `PAGE_SIZE=4096` and 2× `STACK_SIZE=64_KiB` into single `CONFIG_PAGE_SIZE` / `CONFIG_STACK_SIZE` references.

- [ ] extend the makefile for the target check-config. If invoked the configuration gets checked
For the following items: consistency, plausibility, completeness, dependencies and ensure
Upper and lower boundaries.

#### FreeRTOS → Jarvis Config Mapping

| FreeRTOS Config | Jarvis Equivalent | Status |
|---|---|---|
| `configUSE_PREEMPTION` | `CONFIG_PREEMPTION` | Runtime via `BootParams` now; add compile-time default |
| `configCPU_CLOCK_HZ` | `CONFIG_CPU_CLOCK_HZ` | N/A yet (no cycle counter abstraction); add when HPET lands (v0.3.1) |
| `configSYSTICK_CLOCK_HZ` | `CONFIG_TIMER_CLOCK_HZ` | PIT fixed at 1193182; HPET will make this configurable |
| `configTICK_RATE_HZ` | `CONFIG_TICK_HZ` | Currently `BootParams::timer_hz`, default 1000 |
| `configMAX_PRIORITIES` | `CONFIG_PRIORITY_CEILING` | Currently `BootParams::scheduler_priority_ceiling=255` |
| `configMINIMAL_STACK_SIZE` | `CONFIG_MIN_STACK_SIZE` | Not enforced; add minimum for task creation |
| `configMAX_TASK_NAME_LEN` | `CONFIG_TASK_NAME_LEN` | Task names not yet length-limited; add with config |
| `configUSE_16_BIT_TICKS` | _(not needed)_ | Native 64-bit tick count; 16-bit not relevant |
| `configIDLE_SHOULD_YIELD` | `CONFIG_IDLE_YIELD` | Idle task currently busy-waits; could yield to lower power |
| `configUSE_TASK_NOTIFICATIONS` | `CONFIG_INCLUDE_SYS_NOTIFY` | Gate the notify/wait syscalls |
| `configUSE_MUTEXES` | `CONFIG_INCLUDE_MUTEX` | Already present; add compile-time toggle |
| `configUSE_RECURSIVE_MUTEXES` | `CONFIG_INCLUDE_RECURSIVE_MUTEX` | Not yet implemented; gate when added |
| `configUSE_COUNTING_SEMAPHORES` | `CONFIG_INCLUDE_SEMAPHORE` | Already present; add compile-time toggle |
| `configQUEUE_REGISTRY_SIZE` | `CONFIG_QUEUE_REGISTRY_SIZE` | Debug registry for queue monitoring |
| `configUSE_QUEUE_SETS` | `CONFIG_INCLUDE_QUEUE_SET` | Not yet implemented |
| `configUSE_TIME_SLICING` | `CONFIG_TIME_SLICING` | Currently always preemptive; could add round-robin mode |
| `configNUM_THREAD_LOCAL_STORAGE_POINTERS` | `CONFIG_TLS_POINTERS` | TLS not yet supported; add when implementing |
| `configSTACK_DEPTH_TYPE` | `CONFIG_STACK_DEPTH_TYPE` | Use `size_t`; could be `uint16_t` for small-footprint |
| `configHEAP_CLEAR_MEMORY_ON_FREE` | `CONFIG_HEAP_CLEAR_ON_FREE` | Security: zero freed memory before returning to pool |
| `configSUPPORT_STATIC_ALLOCATION` | `CONFIG_STATIC_ALLOC` | Safety-critical: fixed pools, no runtime heap |
| `configSUPPORT_DYNAMIC_ALLOCATION` | `CONFIG_DYNAMIC_ALLOC` | Toggle kernel heap (`new`/`delete`/`kmalloc`) |
| `configTOTAL_HEAP_SIZE` | `CONFIG_HEAP_TOTAL_SIZE` | Sum of all MemPool block totals (~254 KiB currently) |
| `configAPPLICATION_ALLOCATED_HEAP` | `CONFIG_APP_HEAP` | User-provided heap region (vs. kernel-internal) |
| `configSTACK_ALLOCATION_FROM_SEPARATE_HEAP` | `CONFIG_SEPARATE_STACK_HEAP` | Dedicated page region for kernel stacks |
| `configUSE_IDLE_HOOK` | `CONFIG_IDLE_HOOK` | Callback invoked each idle iteration |
| `configUSE_TICK_HOOK` | `CONFIG_TICK_HOOK` | Callback invoked on each timer tick |
| `configCHECK_FOR_STACK_OVERFLOW` | `CONFIG_STACK_CHECK` | Guard-page or canary-based overflow detection |
| `configUSE_MALLOC_FAILED_HOOK` | `CONFIG_OOM_HOOK` | Callback on kernel allocation failure |
| `configUSE_DAEMON_TASK_STARTUP_HOOK` | `CONFIG_INIT_HOOK` | Callback after daemon initialization |
| `configGENERATE_RUN_TIME_STATS` | `CONFIG_SCHED_STATS` | Per-task CPU usage counters (`/proc/sched` in v0.3.2) |
| `configUSE_TRACE_FACILITY` | `CONFIG_TRACE` | Instrumentation trace buffer for debug |
| `configUSE_STATS_FORMATTING_FUNCTIONS` | `CONFIG_STATS_FORMAT` | Human-readable stats output (shell `tasks` command) |
| `configUSE_TIMERS` | `CONFIG_INCLUDE_TIMERFD` | Software timer subsystem (`timerfd` or `/dev/timer`) |
| `configTIMER_TASK_PRIORITY` | `CONFIG_TIMER_TASK_PRIO` | Priority of internal timer management task |
| `configTIMER_TASK_STACK_DEPTH` | `CONFIG_TIMER_TASK_STACK` | Stack size for timer task |
| `configASSERT` | `CONFIG_ASSERT` | Custom assertion macro (replace `JARVIS_ASSERT`) |
| `configENABLE_FPU` | `CONFIG_HAS_FPU` | FPU context switch (FXSAVE/FXRSTOR) — v0.2.16 |
| `configENABLE_MPU` | `CONFIG_HAS_MPU` | Memory protection regions (future, not x86-64 native) |
| `configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY` | `CONFIG_RESTRICT_SYSCALLS` | Limit syscall availability per ring level |
| `INCLUDE_vTaskPrioritySet` | `CONFIG_INCLUDE_SYS_SETPRIO` | Syscall gating for code size |
| `INCLUDE_vTaskDelete` | `CONFIG_INCLUDE_SYS_EXIT` | Syscall gating |
| `INCLUDE_vTaskSuspend` | `CONFIG_INCLUDE_SYS_SUSPEND` | Syscall gating |
| `INCLUDE_vTaskDelay` | `CONFIG_INCLUDE_SYS_NANOSLEEP` | Syscall gating |
| `INCLUDE_xTaskGetCurrentTaskHandle` | `CONFIG_INCLUDE_SYS_GETPID` | Syscall gating |
| `INCLUDE_uxTaskGetStackHighWaterMark` | `CONFIG_INCLUDE_SYS_STACK_INFO` | Syscall gating |


### 0.2.21 — ARM & RISC-V Portability

Builds on the v0.2.20 `jarvis_config.h` HAL to make Jarvis compile and boot on ARM Cortex-A and RISC-V (RV64) hardware. Every architecture-dependent surface (page tables, interrupts, context switch, timer, boot) gets an `arch/<isa>/` implementation selected by the build system.

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

### 0.3.1 — Deterministic Scheduling
- [ ] O(1) bitmap scheduler (per-priority queues, __builtin_clzll), HPET driver (10 kHz)
- [ ] Deadline monitoring with overrun callbacks

### 0.3.2 — Scheduling Analytics & Idle-Task Resource Stewardship
- [ ] WCRT analysis, SYS_SCHED_INFO, /proc/sched metrics
- [ ] Idle task: precise CPU utilisation tracking (rolling 64-bit execution counter in idle loop; export un-inflated CPU load via /proc/sched), all preemtiable and incrementally. Idle task
Never holds and locks any resources.
- [ ] Idle task: slab allocator page return — scan entirely empty slabs/caches during idle cycles, return hoarded page frames to the PMM buddy allocator
- [ ] Deterministic userspace memory pools (slab/buddy, O(1) allocation)

### 0.3.3–0.3.4 — Inheritance & Ceiling
- [ ] Priority inheritance (PIP), priority ceiling (PCP), lock ordering, cyclic-detection engine
- [ ] Hardware budget enforcement, SYS_MLOCK/SYS_MLOCKALL

### 0.3.5 — WCET & Tuning
- [ ] Syscall WCET tracking, kernel stack profiler, allocation-free IRQ paths
- [ ] Interrupt latency measurement, jitter benchmarking suite

### 0.3.6 — ISR Safety & Data Loss Prevention
- [ ] Lock-free SPSC ring buffer for ISR→task handoff (eliminate priority inversion and data loss in interrupt context)

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
