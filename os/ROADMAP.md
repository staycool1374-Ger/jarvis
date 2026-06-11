# Jarvis RTOS — Sequential Development Roadmap

## Phase 1: Code Refactoring & Microkernel Foundation (0.2.7–0.2.10)

### Version 0.2.7 — Error Assertion Retrofit
- [x] **Infrastructure:** New `ASSERT(err)` (debug-only non-fatal error logging) + `ENSURE(cond)` (fatal invariant) macros in `assert.hpp`. Template-based `error_string<E>` specialisation per module. (`84b9d64`)
- [x] **Module error headers:** `task/task_errors.hpp`, `memory/pmm_errors.hpp`, `memory/vmm_errors.hpp` (X-macro pattern). (`84b9d64`)
- [x] **Already completed:** task, scheduler, PMM, VMM, MemPool, IPC — all ASSERT/ENSURE call sites done. (`84b9d64`)
- [ ] **Sync primitives audit:** Add `assert.hpp` + `ASSERT(err)` at allocation/state failure paths in `sync/mutex.cpp`, `sync/semaphore.cpp`, `sync/queue.cpp`, `sync/eventgroup.cpp`, `sync/notify.cpp`.
- [ ] **Syscall handlers audit:** Retrofit `syscall/syscall_handlers_process.cpp`, `syscall/syscall_handlers_fs.cpp`, `syscall/syscall_handlers_ipc.cpp`, `syscall/syscall_handlers_sync.cpp`, `syscall/syscall_handlers_misc.cpp` with error assertions at each failure return.
- [ ] **VFS audit:** Retrofit `vfs/vfs.cpp` (resolve, mount, FdTable), `vfs/initrd_fs.cpp` (MemPool allocs), `vfs/devfs.cpp` (vnode ops), `vfs/procfs.cpp` (PID allocs), `vfs/pipe.cpp` (new allocations, semaphore ops).
- [ ] **ELF loader audit:** Add `ASSERT(err)` at PMM/VMM allocation failures in `elf/elf.cpp`.
- [ ] **Driver registry audit:** Retrofit `driver/driver.cpp` for `new Driver` OOM paths.
- [ ] **Boot & entry audit:** Retrofit `kernel/kernel.cpp` (boot sequence, exception handler, OOM killer) and `kernel/bootparams.cpp`.

### Version 0.2.8 — Code Refactoring & Streamlining ✓
- [x] **O(1) Syscall Dispatch:** Replacement of the linear syscall switch dispatcher with a static function pointer table to eliminate context latency.
- [x] **Modern Syscall Entry:** Migration of the syscall entry sequence from legacy `int $0x80` software interrupts to native, high-frequency `syscall/sysret` paths via MSRs (`IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`). MSR infrastructure (`arch::wrmsr`/`arch::rdmsr`), `Syscall::init()` configures all three MSRs. `syscall_entry.asm` ready. Using the `syscall` instruction in userspace is planned for a follow-up session (requires fixing register mapping in the entry asm and setting up `KERNEL_GS_BASE`).
- [x] **Strict Type Safety:** Complete elimination of raw `reinterpret_cast` constructs from the kernel core. Implementation of type-safe abstraction wrappers for `PhysicalAddress` and `VirtualAddress` in `address.hpp` (`phys_to_virt`/`virt_to_phys` conversions, `PageAddress` for page-aligned phys addresses).
- [x] **Unified Logging & Hardware Abstraction:** Introduction of an abstract `Arch::Serial` hardware class (replacing raw `outb` directives) coupled with a central `Logger` interface. Complete tree-wide purge of inline assembly outside `arch/` (CR read/write accesses extracted into `arch::read_cr*`/`arch::write_cr3`).
- [x] **Linker Optimizations:** Integration of linker optimizations for the release target (`-ffunction-sections`, `-fdata-sections`) to enable efficient section garbage collection in future iterations. LTO deferred due to missing LLD integration in the cross-toolchain.
- [x] **Code Quality Metrics:** Function separation rule (syscall files split into 6 domains). Replacement of `strncpy` with `strlcpy` in `sys_umame`.
- [x] **Const Correctness:** Declaration of all immutable parameters and methods as `const` in `PhysicalAddress`, `VirtualAddress`, `PageAddress`, the PMM/VMM interface, and other core components.
- [x] **Reference Parameters:** Preference of reference parameters (`const T&`) over value parameters for non-POD types to avoid unnecessary copies and document aliasing contracts.

### Version 0.2.9 — Microkernel Foundation & Task Lifecycle
- [x] **Subsystem Privilege Audit:** Structured cataloging of all kernel subsystems by their actual privilege requirements for clean separation of Ring-0 and Ring-3 content. (`docs/privilege_audit.md`)
- [x] **IPC Latency Benchmark:** Development of a measurement suite (7 kernel-level micro-benchmarks using RDTSC). Measures IPC send/recv costs (avg 235 cycles), recv only (50), 64B payload (175). Baseline for microkernel comparison.
- [x] **Unprivileged Shell:** Execution of the shell as an unprivileged user-space task (`sh.c.elf`, PID 2B, Ring 3). Kernel shell registered as fallback. Zero additional IPC latency — standard VFS `read("/dev/tty")` and `write()` syscalls via MSR fast path. (`kernel.cpp:278-295`)
- [ ] **Task Lifecycle Audit:** Systematic check whether terminated tasks and all their dependencies (IPC queues, Notify, EventGroup, page tables, FDs) are fully destroyed and removed from the scheduler. Fix the bugs found:
  - Fix `reap_orphans()` can-reap logic (sets `can_reap=true` on first loop iteration — prevents correct deferred cleanup)
  - `elf::load()` does not call `init_task_common()` → `msg_queue`/`notify`/`event_group` are `nullptr` for ELF-loaded tasks (IPC crash)
  - Blocked IPC senders (`blocked_senders`) are abandoned on task exit — tasks remain `BLOCKED` forever
  - Fork child with `page_table_shared_=true` calls `free_user_pages()` → use-after-free in parent
- [ ] **Userspace Idle Task:** Implementation of a simple idle task in Ring 3 (`idle.c.elf`), running as a low-priority background task:
  - Infinite `pause` loop (userspace-safe, unlike `hlt`)
  - Added to the scheduler at boot (lowest priority)
  - Kernel `hlt` idle (`tasks_[0]`) remains as the ultimate fallback
  - Later extensible (power management, CPU frequency scaling, deferred work)

### Version 0.2.10 — User-space Server Infrastructure
- [ ] **Zero-Copy Buffer Pool:** Introduction of a fixed pool of pre-allocated 4 KiB buffers in kernel memory (~256 KiB, 64 buffers) transferable between tasks via handle-transfer through IPC — no copying of payload data. Enables efficient pipeline communication for VFS and IOCD servers and later networking.
  - `BufferPool` class with bitmap allocator: `alloc(pid)`, `free(handle)`, `free_all(pid)` cleanup on task death
  - IPC extension: `send_buffer(dest, handle)` and `recv_buffer(handle)` — handle encoded in existing `Message.data[]`, no struct enlargement
  - Buffers directly addressable via HHDM, no page table manipulation in the first step
  - 6 files, ~160 lines of implementation
- [ ] **User-space VFS Server:** Migration of the virtual filesystem (VFS) out of the kernel into a standalone, privileged user-space server process (`/sbin/vfsd`) communicating purely via IPC.
- [ ] **User-space Driver Server:** Migration of keyboard and serial drivers into an isolated user-space driver process (`/sbin/iocd`) via IPC.
- [ ] **Capability-based Hardware Access:** Implementation of a tamper-proof capability access control for device memory, so MMIO regions can be mapped exclusively for authorized driver tasks.

---

## Phase 2: Filesystems & Shell UX (0.2.11–0.2.12)

### Version 0.2.11 — FAT32 Block Filesystem
- [ ] **ATA PIO Driver:** Implementation of a native ATA PIO driver for the QEMU IDE interface (I/O ports, sector-based read/write).
- [ ] **Block Device Abstraction Layer:** Development of a standardized block layer in the VFS for unified management of sectors, buffers, and `ioctl` calls.
- [ ] **FAT32 Driver Core:** Implementation of the FAT32 core including MBR parsing for partition localization, FAT table caching, cluster chain traversal, and support for short 8.3 filenames.
- [ ] **Storage Integration:** Creation of a 128 MiB FAT32 disk image via `mkfs.fat`, integration into QEMU (`-drive file=disk.img,format=raw,if=ide`), and provisioning as a mountpoint under `/home`.
- [ ] **VFS FAT32 Operations:** Elaboration of VFS interfaces for FAT32 (`open`, `read`, `write`, `close`, `mkdir`, `unlink`, `fstat`, `readdir`) so the shell can manipulate files in the mount path.
- [ ] **Rootfs Preparation:** Integration of architectural hooks to enable booting the kernel directly from a FAT32 partition instead of the initrd in future iterations.

### Version 0.2.12 — Shell UX & Utilities
- [ ] **Persistent Status Bar:** Implementation of a persistent status bar in the framebuffer terminal (inverted text line at the bottom) and serial terminal (ANSI escape sequences) for live display of date/time, CPU%, memory%, and the exit code of the last command.
- [ ] **Prompt Extension:** Upgrade of the shell prompt to a dynamic, Zsh-like format (`user@host:/pwd$`) with native updates on directory changes and support for a configurable `PROMPT` environment variable.
- [ ] **Shell Builtins Extension:** Direct embedding of the `help`, `echo`, `pwd`, `clear`, `which`, `env`, and `sleep` commands as built-in shell routines.
- [ ] **Directory Syscalls:** Provision of the `SYS_MKDIR` and `SYS_UNLINK` system calls in the kernel to support external file manipulation.
- [ ] **Standalone Initrd Utilities:** Compilation of clean, standalone user-space binaries for `mkdir`, `rm`, `rmdir`, `echo`, `pwd`, `clear`, and `sleep` within the initrd image.
- [ ] **IPC Pipeline Robustness:** Hardening and bug fixing of the shell execution loop for complex chaining (pipes `|` and I/O redirections `>`, `<`).

---

## Phase 3: System Services & Hardware (0.2.13–0.2.16)

### Version 0.2.13 — System Services
- [ ] **tmpfs (Temporary Filesystem):** Development of a volatile, RAM-based filesystem with dynamic page allocation and hard, user-specific quota limits to prevent OOM exploits. Mount under `/tmp` during boot.
- [ ] **Init System (PID 1):** Design of `/sbin/init` as the primary user-space ELF task, monitoring the service lifecycle, catching crashes, and controlling the boot process via structured `/etc/rc` scripts.
- [ ] **Automated Mounts (fstab):** Specification and parser implementation for an `/etc/fstab` configuration file for automated mounting of all system filesystems before shell start.
- [ ] **Resource Limits & Heap Extension:** Implementation of `SYS_GETRLIMIT`/`SYS_SETRLIMIT` in the TCB (monitoring `max_fds`, `max_stack`, `max_memory`) as well as integration of `SYS_BRK` for dynamic heap expansion (`sbrk`/`brk` in libc).
- [ ] **Core Text Utilities:** Provision of a lightweight interactive text pager (analogous to `less`) and a line-based editor (analogous to `ed`) as native system binaries.

### Version 0.2.14 — Hardware Enablement
- [ ] **PCI Enumeration:** Implementation of PCI configuration space access (I/O ports `0xCF8`/`0xCFC`), scanning bus 0, parsing capability lists (MSI, MSI-X, PM), and dynamic BAR resource allocation.
- [ ] **Virtio Transport & Drivers:** Development of the Virtio transport layer (`virtio-mmio` or `virtio-pci`) including descriptor rings. Writing a high-performance `virtio-blk` driver as a FAT32 backend alternative.
- [ ] **Minimal Network Stack:** Implementation of a compact, real-time capable network protocol stack for ARP, IPv4, and UDP directly over an emulated `virtio-net` device.

### Version 0.2.15 — CPU Features & RNG
- [ ] **FPU/SSE Context Switch:** Integration of lazy FPU/SSE state backup via `FXSAVE`/`FXRSTOR` through a dedicated 512-byte buffer in `TaskControlBlock` on each thread switch.
- [ ] **Entropy & Crypto RNG:** Integration of hardware-based random number generation (`RDRAND`/`RDSEED`) coupled with a software ChaCha20 PRNG, exported via `/dev/random` and `/dev/urandom` through Devfs and via `SYS_GETRANDOM`.

### Version 0.2.16 — Observability & Portability
- [ ] **Kernel Log Ring Buffer:** Creation of a persistent, fixed in-memory ring buffer for all kernel log entries, readable in user-space via the new `SYS_KLOG` syscall and a companion `dmesg` utility.
- [ ] **Hardware Abstraction Layer (HAL):** Formal encapsulation of hardware-specific logic behind a clean interface framework in `arch/hal.hpp` (`ArchPageTable`, `ArchContext`, `ArchInterruptController`, `ArchTimer`, `ArchIO`).
- [ ] **Structural Directory Migration:** Migration of all x86_64-specific code (including the syscall entry points) from the generic `arch/` path into the dedicated `arch/x86_64/` subdirectory.
- [ ] **Multi-Arch Build System:** Extension of the Makefile with a global `ARCH` variable for easy control of cross-compiler toolchains (default: x86_64).
- [ ] **Secure Exec & Regression Audit:** Refactoring of `SYS_EXEC` for element-wise validation of `argv` and `envp` via a `CheckedPointer<T>` construct (returning `-EFAULT` instead of kernel panic). Validation of all 492+ regression tests after the HAL restructure.

---

## Phase 4: Hard Real-Time (0.3.x)

### Version 0.3.1 — Deterministic Scheduling & High-Precision Timers
- [ ] **O(1) Bitmap Scheduler:** Replacement of the linear `tasks_[64]` array scan with a bitmap-indexed, strictly priority-ordered per-priority queue structure. `next_task()` reduced from O(n) to O(1) using `__builtin_clzll(occupied_)`. `needs_switch()` reduced from O(n) to O(1) via bitmask comparison. Round-robin within equal priority. Prerequisite for WCRT analysis, deadline monitoring, and priority inheritance.
  - Introduction of `sched_next` pointer in `TaskControlBlock`
  - New scheduler states: `queue_heads_[64]`, `queue_tails_[64]`, `occupied_` bitmap, `current_task_` pointer
  - Rewrite of `add_task`, `remove_task`, `next_task`, `needs_switch`, `reap_orphans`
  - Replacement of `task_at(i)` iterations in external callers with `for_each_task(callback)`
- [ ] **HPET Driver:** Implementation of a High Precision Event Timer driver to raise the kernel tick frequency to a precise 10 kHz (replacing the imprecise 1 kHz PIT).
- [ ] **Deadline Monitoring:** Introduction of strict real-time deadline checks based on `deadline_ticks` bounds, paired with an asynchronous overrun callback infrastructure.

### Version 0.3.2 — Scheduling Analytics & Deterministic Memory
- [ ] **Periodic Release Controls:** Integration of automated scheduler cycles for precise release of periodic tasks and reset of the remaining budget (`remaining_ticks`).
- [ ] **WCRT Analysis:** Embedding of analytical profilers to determine the worst-case response time (WCRT) by capturing execution peaks (`executed_ticks_max`) per task.
- [ ] **Scheduler Observability:** Provision of real-time performance data via the `SYS_SCHED_INFO` syscall and mapping of task metrics in the synthetic filesystem under `/proc/sched`.
- [ ] **Deterministic User-space Pools:** Provision of pre-allocated user-space memory pools (slab/buddy structures) to guarantee O(1) allocations in the real-time context without the risk of lazy PMM kernel fallbacks.

### Version 0.3.3 — Priority Inheritance & Budget Enforcement
- [ ] **Static Resource Hierarchy:** Enforcement of a strict lock ordering via predefined, static resource IDs for mathematical elimination of deadlocks in nested locking scenarios.
- [ ] **Hardware-Based Budget Enforcement:** Configuration of the hardware timer for immediate, hard task preemption exactly when the assigned thread budget reaches `remaining_ticks == 0`.
- [ ] **Complete PIP (Priority Inheritance):** Implementation of the complete priority inheritance protocol across all internal IPC transaction layers and nested task dependency paths.

### Version 0.3.4 — Priority Ceiling & Deadlock Prevention
- [ ] **Complete PCP (Priority Ceiling):** Native support of the priority ceiling protocol for kernel mutex objects to structurally prevent priority inversions and deadlocks before they occur.
- [ ] **Asynchronous Lock Validation Engine:** Development of a run-time active verification algorithm (in debug builds) to detect illegal, cyclic lock requests.
- [ ] **Real-Time Memory Pinning:** Implementation of the `SYS_MLOCK` and `SYS_MLOCKALL` syscalls to permanently pin physical pages of protected real-time tasks in RAM and completely eliminate page fault latencies.

### Version 0.3.5 — WCET Analysis & Tuning
- [ ] **Syscall WCET Tracking:** Integration of automated monitors for precise calculation of worst-case execution time (WCET) across all exposed system calls (capturing min/max/avg execution times).
- [ ] **Kernel Stack Usage Profiler:** Development of a background task for continuous monitoring and recording of the maximum stack nesting depth per execution thread.
- [ ] **Allocation-Free IRQ Paths:** Complete elimination of any dynamic heap allocations within critical I/O and interrupt processing paths to ensure temporal predictability.
- [ ] **Interrupt Latency Measurement:** Implementation of hardware-clocked measurement routines using CPU cycle counters to capture the exact hardware interrupt response time variance.
- [ ] **Jitter Benchmarking Suite:** Creation of a synthetic load scenario for continuous quantification of scheduling jitter under maximum system load.

---

## Phase 5: SMP + Multicore (0.4.x)

### Version 0.4.1 — APIC & Interrupt Routing
- [ ] **Per-CPU Data Isolation:** Instantiation of isolated GDT and TSS structures per CPU core, protected by global, highly-optimized spinlock barriers.
- [ ] **Local APIC Initialization:** Activation and configuration of the local APIC (including X2APIC topology) on the bootstrap processor (BSP) for fine-grained interrupt routing.
- [ ] **I/O APIC Routing:** Configuration of the I/O APIC for deterministic distribution of external hardware interrupt vectors to dedicated CPU cores.

### Version 0.4.2 — SMP Boot & Core Isolation
- [ ] **Multicore Symmetric Boot:** Implementation of the SMP startup sequence for controlled wake-up of secondary application processors (APs) using the standardized INIT-SIPI inter-processor interrupt sequence.
- [ ] **Core State Isolation:** Physical separation of system tables by assigning individual PML4 page directories, kernel stacks, and TSS descriptors per active core.
- [ ] **Hardware Interrupt Prioritization:** Use of the APIC task priority register (TPR) to establish a strict hardware interrupt priority level that blocks interruption of critical tasks by low-priority IRQs.

### Version 0.4.3 — Per-CPU Scheduling & Load Balancing
- [ ] **Distributed Run Queues:** Architecture redesign of the scheduler away from a central lock toward self-contained, isolated run queues per CPU core for scalability optimization.
- [ ] **Real-Time Load Balancer:** Integration of symmetric load balancing algorithms that distribute threads deterministically across cores using idle-pull and work-push metrics.
- [ ] **Core Affinity:** Provision of the `SYS_SET_AFFINITY` and `SYS_GET_AFFINITY` syscalls in the kernel and embedding of the corresponding wrappers in libc for targeted task binding.

### Version 0.4.4 — Cache & SMP Synchronization
- [ ] **Cache Coloring Allocator:** Development of a cache-sensitive page allocator to minimize destructive L2/L3 cache line evictions under parallel core utilization.
- [ ] **SMP Synchronization Primitives:** Deployment of highly efficient, hardware-near SMP spinlocks and reader-writer locks throughout the kernel tree to protect concurrent data streams.
- [ ] **Real-Time SMP Verification Audit:** Complete mathematical re-verification and metrological auditing of all single-core WCET and WCRT bounds under active SMP lock contention and maximum bus load.

### Version 0.4.5 — TLB Shootdown Optimization
- [ ] **PCID Support:** Activation of process context identifiers (PCID) in the control registers to prevent complete TLB entry invalidation on address space switches.
- [ ] **Selective TLB Invalidation:** Use of the `INVPCID` instruction set for targeted, fine-grained invalidation of specific pages instead of a global TLB flush.
- [ ] **Lazy TLB Shootdowns:** Implementation of a lazy TLB shootdown procedure for pages mapped across multiple cores in shared memory scenarios.

### Version 0.4.6 — IPI Reduction & Latency Profiling
- [ ] **IPI Avoidance Strategies:** Development of batching and deferral strategies to drastically reduce the number of expensive inter-processor interrupts (IPIs) during a TLB shootdown.
- [ ] **Latency Profiling:** Integration of automated metrics for exact comparison of measured TLB shootdown latency relative to the number of issued cross-core IPIs.
- [ ] **PML4 Synchronization Integration:** Deep integration of the shootdown optimization layer directly within the per-CPU PML4 update routines of the kernel.

---

## Phase 6: System Integration (0.5.x)

### Version 0.5.1 — System Integration Testing
- [ ] **Integration Testing:** Provision of comprehensive test suites for cross-boundary flows; execution of a 24-hour continuous stress test ensuring a deadline overrun rate of < 0.01%; benchmark capture of IPC throughput and context switches.

### Version 0.5.2 — Safety Hardening & Release
- [ ] **Safety Hardening:** Documentation of syscall determinism for safety certificates; pointer isolation audit; injection of stack cookies via GCC `-fstack-protector`; creation of optimized release builds (`-O3 -DNDEBUG`, stripped); Doxygen export.

---

## Phase 7: Safety Systems (0.6.x)

### Version 0.6.1 — Hardware Watchdog
- [ ] **Hardware Watchdog:** Initialization of native ICH9/HPET watchdog circuits; implementation of NMI service routines for pre-timeout warnings; provision of `SYS_WATCHDOG_KICK`; PIT software fallback when hardware is missing.

### Version 0.6.2 — Per-Task Software Watchdog
- [ ] **Software Watchdog:** Implementation of a software-based monitoring system at the task level via `SYS_WATCHDOG_CREATE(period_ms)` and `SYS_WATCHDOG_KICK`; automatic task termination on expiry; state export via `/proc/[pid]/watchdog`.

### Version 0.6.3 — Deadlock Detection & Recovery
- [ ] **Deadlock Detection & Recovery:** Run-time generation of dynamic resource dependency graphs (wait-for-graphs); non-blocking detection loops via the watchdog infrastructure; targeted isolation and forced termination of blocked threads along with resource reclamation; provision of `SYS_HEALTH_STATUS`.
