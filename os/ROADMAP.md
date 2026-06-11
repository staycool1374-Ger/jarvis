# Jarvis RTOS — Development Roadmap

## Phase 1: Code Refactoring & Microkernel Foundation (0.2.7–0.2.11)

### 0.2.7 — Error Assertion Retrofit ✓
- [x] ASSERT(err) / ENSURE(cond) macros + per-module error_string<E> specialisation
- [x] Module error headers: task, PMM, VMM (X-macro pattern)
- [x] Retrofitted: task, scheduler, PMM, VMM, MemPool, IPC — all ASSERT/ENSURE call sites
- [x] Retrofit sync (mutex, semaphore, queue, eventgroup, notify)
- [x] Retrofit syscall handlers (process, fs, ipc, sync, misc)
- [x] Retrofit VFS (resolve, mount, FdTable, initrd_fs, devfs, procfs, pipe)
- [x] Retrofit ELF loader, driver registry, kernel.cpp, bootparams.cpp

### 0.2.8 — Code Refactoring & Streamlining ✓
- [x] O(1) syscall dispatch table, MSR-based syscall/sysret entry
- [x] Type-safe PhysicalAddress/VirtualAddress wrappers, no raw reinterpret_cast
- [x] Unified Arch::Serial + Logger, inline-asm purged from non-arch/ code
- [x] Linker GC, function/data sections, const correctness, reference params

### 0.2.9 — Microkernel Foundation & Task Lifecycle ✓
- [x] Privilege audit, IPC latency benchmark, unprivileged userspace shell
- [x] Task lifecycle audit: reap_orphans fix, elf::load init_task_common, blocked-sender cleanup, page_table_shared_ use-after-free
- [x] Userspace idle task (Ring 3 pause loop)
- [x] Userspace server infrastructure: VFS daemon (vfsd), I/O daemon (iocd)
- [x] Capability-based MMIO access control
- [x] Wait-for-graph deadlock detection engine (WFG)
- [x] Deadlock recovery: terminate task, release locks, unblock waiters
- [x] Health status syscall and /proc/health metrics
- [x] Full audit: bugs #009-#019 closed
- [x] 1233 kernel self-tests PASS

### 0.2.10 — Zero-Copy Buffer Pool
- [x] Zero-copy buffer pool (~256 KiB, 64 buffers, handle-transfer via IPC)
- [x] Userspace VFS server (vfsd) hardening + crash recovery
- [x] I/O daemon (iocd) hardening + crash recovery

### 0.2.11 — Coding Style Refactoring (current)
- [ ] Const correctness retrofit — `const` on all kernel variables, params, member functions
- [ ] References over pointers — migrate non-nullable `T*` params to `T&`
- [ ] All variables initialized — fix every uninitialized local declaration
- [ ] Constructor init-list migration — member assignments in body → member initializer list
- [ ] Meaningful sentinel enums — replace raw `-1` checks with named constants
- [ ] Descriptive names — rename blocklisted single-char vars (`t`, `v`, `val`, `tmp`, `ptr`, `p`)
- [ ] Remove `const_cast` — use `mutable` or redesign to avoid const modification
- [ ] Bounded loops — replace unbounded `while (true)`/`for (;;)` with max-iteration guards
- [ ] Dynamic allocation audit — replace `new`/`delete` on kernel paths with fixed pools
- [ ] Documentation Doxygen compliance — `@brief`, `@param`, `@return` on all public APIs
- [ ] Validation — zero errors from `make check-style` (exit 0)

---

## Phase 2: Filesystems & Shell UX (0.2.12–0.2.13)

### 0.2.12 — FAT32 Block Filesystem
- [ ] ATA PIO driver, block device abstraction layer
- [ ] FAT32 core: MBR parsing, cluster chain, 8.3 filenames
- [ ] 128 MiB disk image, VFS FAT32 ops, rootfs boot hooks

### 0.2.13 — Shell UX & Utilities
- [ ] Persistent status bar (framebuffer + serial), Zsh-like dynamic prompt
- [ ] Built-in commands: help, echo, pwd, clear, which, env, sleep
- [ ] SYS_MKDIR/SYS_UNLINK, standalone initrd utilities (mkdir, rm, rmdir, etc.)
- [ ] IPC pipeline hardening (pipes, I/O redirection)

---

## Phase 3: System Services & Hardware (0.2.14–0.2.17)

### 0.2.14 — System Services
- [ ] tmpfs (/tmp, user quotas), init system (PID 1, /etc/rc), fstab automount
- [ ] SYS_GETRLIMIT/SYS_SETRLIMIT, SYS_BRK, text pager/editor utilities

### 0.2.15 — Hardware Enablement
- [ ] PCI enumeration (CF8/CFC, BAR, MSI/MSI-X), Virtio transport + blk driver
- [ ] Minimal network stack (ARP, IPv4, UDP over virtio-net)

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

### 0.3.2 — Scheduling Analytics
- [ ] WCRT analysis, SYS_SCHED_INFO, /proc/sched metrics
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

---

## Phase 7: Safety Systems (0.6.x)

### 0.6.1–0.6.2 — Watchdogs
- [ ] ICH9/HPET hardware watchdog + NMI pre-timeout, PIT fallback, SYS_WATCHDOG_KICK
- [ ] Per-task software watchdog (SYS_WATCHDOG_CREATE), /proc/[pid]/watchdog

### 0.6.3 — Deadlock Detection
- [ ] Wait-for-graph runtime, watchdog-driven detection, forced recovery, SYS_HEALTH_STATUS
