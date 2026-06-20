# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 3 SYSTEM SERVICES MODE
**Status:** ACTIVE — System Services.
**Target Focus:** v0.12.14 — tmpfs implementation, PID 1 Initialization, and Memory Expansion (`SYS_BRK`).

## 1. Safety & Concurrency Guardrails (Strict)
- **Preserve IrqGuard Invariants:** Any new service layer code, VFS operations for `tmpfs`, or memory-mapping extensions must strictly utilize the `src/kernel/arch/irq_guard.hpp` abstraction for critical sections. Do not use open-coded `cli()`/`sti()` or unmanaged interrupt modifications.
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

## Phase 3: System Services & Hardware (v0.12.14–v0.2.17)

### 0.2.16 — CPU Features & RNG
- [ ] Lazy FPU/SSE context switch (FXSAVE/FXRSTOR)
- [ ] Hardware RNG (RDRAND/RDSEED) + ChaCha20 PRNG → /dev/random, SYS_GETRANDOM

### 0.2.17 — Observability & Portability
- [ ] Kernel log ring buffer (SYS_KLOG, dmesg), HAL abstraction, arch/x86_64/ migration
- [ ] Multi-arch build (ARCH variable), secure exec (CheckedPointer), regression audit
- [ ] PCI bus enumeration / device tree debug output (pci_print_tree, sysfs /proc/pci)

### 0.2.18 — Kernel Memory Safety
- [ ] `UniquePtr<T>` / `make_unique` — type-safe RAII wrapper for kernel heap allocations (placement-new construction, move-only ownership, automatic `kfree` + destructor on scope exit)
- [ ] Audit existing `kmalloc`/`kfree` sites for leak-prone manual management and migrate to `UniquePtr` where appropriate
- [ ] Audit existing `new`/`delete` usages in kernel code for consistency with the RAII pattern

### 0.2.19 — System Calls & Storage
- [ ] SYS_YIELD — cooperative task yielding for CPU-bound tasks
- [ ] SYS_REBOOT / SYS_HALT — system power management from userspace
- [ ] AHCI/SATA driver with NCQ (replaces ATA PIO for bare-metal storage)
- [ ] DMA completion interrupt infrastructure (ISR acknowledges and fires for storage I/O)
- [ ] Double-buffered DMA transfer support (ping-pong buffers for streaming storage)

---

## Phase 4: Hard Real-Time (0.3.x)

### 0.3.1 — Deterministic Scheduling
- [ ] O(1) bitmap scheduler (per-priority queues, __builtin_clzll), HPET driver (10 kHz)
- [ ] Deadline monitoring with overrun callbacks

### 0.3.2 — Scheduling Analytics & Idle-Task Resource Stewardship
- [ ] WCRT analysis, SYS_SCHED_INFO, /proc/sched metrics
- [ ] Idle task: precise CPU utilisation tracking (rolling 64-bit execution counter in idle loop; export un-inflated CPU load via /proc/sched)
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
- [ ] Full TCP/IP stack (ARP, IP, ICMP, UDP, TCP) with Ethernet NIC driver
- [ ] Distributed real-time communication, remote logging, networked control systems

### 0.9.2 — USB Stack
- [ ] USB driver stack (UHCI/EHCI/xHCI) for keyboard, mouse, and storage
- [ ] Replace PS/2 with USB HID for real-hardware input
