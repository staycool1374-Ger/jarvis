# Jarvis RTOS — Development Roadmap

# EXECUTIVE OVERRIDE: PHASE 3 SYSTEM SERVICES MODE
**Status:** ACTIVE — System Services.
**Target Focus:** v0.12.14 — tmpfs implementation, PID 1 Initialization, and Memory Expansion (`SYS_BRK`).

## 0.2.13 — Shell Built-ins & Stabilisation
- [x] FAT32 unlink empty-dir fix (skip `.` and `..`)
- [x] `vfs_unlink_file`, `vfs_mkdir_valid` test isolation
- [x] Shell `mkdir` bypasses VFS daemon for absolute paths
- [x] BUGS.md #007 (idle task test output) — Fixed
- [x] 28 new shell built-in commands: `alias`, `unalias`, `history`, `type`, `source` (`.`), `set`, `read`, `printf`, `test` (`[`), `shift`, `trap`, `wait`, `fg`, `bg`, `disown`, `ulimit`, `umask`, `times`, `logout`, `dirs`, `pushd`, `popd`, `ls`
- [x] Alias expansion + command history recording in `parse_and_exec`
- [x] Release tag: v0.2.13

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

### v0.12.14 — System Services
- [x] tmpfs (/tmp, user quotas), init system (PID 1, /etc/rc), fstab automount
- [x] SYS_GETRLIMIT/SYS_SETRLIMIT, SYS_BRK, text pager/editor utilities
- [x] IrqGuard enforcement in all tmpfs operations and sys_brk

### 0.2.15 — Hardware Enablement
- [x] PCI enumeration — CF8/CFC config space access, bus scan, BAR parsing, PCI bridge support
- [x] MSI/MSI-X interrupt support — capability detection, vector allocator, MSI/MSI-X enable
- [x] Virtio transport (modern 1.0 PCI) + block driver — capability parsing, MMIO mapping, feature negotiation, queue setup, block I/O
- [x] DMA driver — physically contiguous buffer alloc, scatter-gather list, PRD table (ATA bus-master format), PCI bus master control
- [x] Minimal network stack — Ethernet/ARP/IPv4/UDP protocol types, ARP cache with resolution, IPv4 header checksum, UDP send/receive, virtio-net NIC driver (modern 1.0 PCI)

### 0.2.16 — CPU Features & RNG
- [ ] Lazy FPU/SSE context switch (FXSAVE/FXRSTOR)
- [ ] Hardware RNG (RDRAND/RDSEED) + ChaCha20 PRNG → /dev/random, SYS_GETRANDOM

### 0.2.17 — Observability & Portability
- [ ] Kernel log ring buffer (SYS_KLOG, dmesg), HAL abstraction, arch/x86_64/ migration
- [ ] Multi-arch build (ARCH variable), secure exec (CheckedPointer), regression audit

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
