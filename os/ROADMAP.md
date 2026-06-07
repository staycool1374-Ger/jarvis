# Jarvis RTOS Roadmap

## Version 0.0.1 (Base)
- [x] x86_64 Long Mode Boot
- [x] Framebuffer Terminal
- [x] PS/2 Keyboard
- [x] Shell with basic commands
- [x] PMM (Physical Memory Manager)
- [x] VMM (Virtual Memory Manager)
- [x] MemPool (Slab Allocator)
- [x] Task/Scheduler (Rate Monotonic)
- [x] IPC (Inter-Process Communication)
- [x] Basic syscall framework
- [x] Demo program (Mandelbrot)

## Version 0.0.2 — Clean Code & Security
- [x] Single-user optimization
- [x] Security review: Null checks, buffer overflow prevention
- [x] Performance review: Inlining, constexpr, overhead reduction
- [x] Clean Code: Naming conventions, const correctness, noexcept

## Version 0.0.3 — Doxygen + Exception Handling + Dynamic Memory
- [x] Doxygen configuration (Doxyfile)
- [x] Header documentation for all modules
- [x] Error-code-based exception handling (ErrorOr\<T\>)
- [x] Heap allocator with PMM fallback
- [x] OOM (Out Of Memory) handling

## Version 0.0.4 — Driver Layer
- [x] Driver registry (Driver registration framework)
- [x] modprobe / modlist shell commands
- [x] Standard hardware drivers compiled as modules
- [x] Driver initialization during boot stage

## Version 0.0.5 — Test Methodology + Benchmark
- [x] Benchmark shell command (cpu, alloc)
- [x] Internal kernel self-test registry
- [x] Self-tests for PMM, String, Utils, ErrorOr, IPC, MemPool
- [x] selftest shell command (run all tests or filter by name)
- [x] Fix: Blank screen bug (triple fault during task context switch)

## Version 0.0.6 — Final Optimization & Stability
- [x] Switched build configurations to -O3
- [x] Resolved all compiler warnings under -O3
- [x] Resolved all compiler errors under -O3
- [x] Mandelbrot CPU benchmark against Linux (RTOS: 35 ms, Linux: 29 ms = 83% performance parity)
- [x] Complete clean code review
- [x] Final optimization round
- [x] Pending bugfixes completed

## Version 0.0.7 — Userspace Infrastructure
- [x] Modified handle_interrupt_c to accept a 4th parameter uint64_t* regs for register access
- [x] Intercepted Vector 0x80 (int $0x80) in handle_interrupt_c -> routed to syscall_handler()
- [x] Unit test: syscall.int80_dispatch (C-level dispatch mechanism)
- [x] Integration test: task.user_execution (User task via scheduler + Ring 3 transition)
- [x] GDT Bugfix: Restricted ltr execution to occur only once inside load()
- [x] create_user Bugfix: Mapped kernel_stack_top as a higher-half virtual address
- [x] EXIT Syscall: Removed infinite while(true) hlt loop, adding reschedule() after marking state as TERMINATED
- [x] Scheduler::reschedule() added for immediate context switching upon process EXIT
- [x] 30/30 Tests PASS

## Version 0.0.8 — ELF Loader + initrd + File Syscalls
- [x] ELF64 loader (validate_header, load) for statically linked ELF binaries
- [x] initrd (cpio newc archive) linked into kernel via objcopy
- [x] File system syscalls: OPEN, READ, CLOSE, FSTAT, WRITE
- [x] Userspace test ELF (main.S.elf, syscall #99 + EXIT) loadable from initrd
- [x] Bugfix: Corrected byte order mapping for Little-Endian translation of ELF_MAGIC
- [x] 36/36 Tests PASS

## Version 0.0.9 — Syscall Return Value + Blocking IPC
- [x] Return value propagation implemented in handle_interrupt_c (regs[0] = result)
- [x] Scheduler Bugfix: Prevented BLOCKED task state from being overwritten
- [x] SYS_SEND: Derived sender_id directly from current_task->id
- [x] SYS_RECEIVE: Configured to block task when the mailbox is empty
- [x] SYS_SEND_SYNC: Implemented send + block + reply sequence
- [x] 57 Tests PASS

## Version 0.0.10 — libc Porting
- [x] Minimal libc for userspace operations (syscall wrappers, crt0, printf, string.h, etc.)
- [x] Standard library headers (stdlib.h, stdio.h, string.h, unistd.h, sys/stat.h, ...)
- [x] Userspace C program (hello.c) loadable and executable via initrd
- [x] Allocated explicit heap region and configured argv stack parsing inside the ELF loader
- [x] Pre-assigned standard streams (stdin, stdout, stderr) to /dev/tty
- [x] Framebuffer higher-half mapping fix (resolved #PF crash during Terminal::scroll)
- [x] 57 Tests PASS

## Version 0.1.0 — Userspace Shell + Programs
- [x] SYS_EXEC syscall (20) — replaces the current task with an ELF binary from a VFS path
- [x] ELF Loader: exec_into_current() — reloads userspace context without allocating a new TCB
- [x] ELF Loader: Configured compliant argv/envp stack layout adhering to System V ABI standards
- [x] Developed userspace C utilities via initrd: ls.c, cat.c, top.c
- [x] Embedded kernel shell built-ins: cd, export, runelf
- [x] initrd_root_readdir — added directory listing functionality on root /
- [x] VMM::free_user_pages() — added clean teardown of old page tables upon exec
- [x] 63 Tests PASS

## Version 0.1.1 — VFS Foundation (Vnode + Per-Task FDs)
- [x] Created Vnode and VnodeOps abstractions backed by explicit function pointer tables
- [x] Implemented a per-task FdTable tracking arrays within the TaskControlBlock (replacing the global fd_table[64])
- [x] Tracked current working directory variables (cwd + cwd_vnode) inside individual TaskControlBlock states
- [x] Defined FileDescription tracking structure containing Vnode pointers, file offset indicators, and operation flags
- [x] Migrated all file system syscall implementations to utilize the unified Vnode API
- [x] Implemented LSEEK (14) and IOCTL (15) syscall architectures
- [x] Relocated strcmp/strncmp routines to the centralized string.hpp module
- [x] 40 Tests PASS

## Version 0.1.2 — Devfs + Mount System
- [x] Created internal Mount Table (fixed array capacity applying longest-prefix-match evaluations)
- [x] Developed path resolution utility resolve(path) with parsing of absolute, relative, root /, and parent .. paths
- [x] Formed Virtual Device File System (Devfs): /dev/tty, /dev/null, /dev/console, /dev/kbd
- [x] Enabled blocking reads on /dev/tty managed via scheduler BLOCKED flags and tty_wake_readers() triggers
- [x] Mounted initrd storage instance directly as a VFS filesystem on root /
- [x] Routed open, read, and write operations cleanly to hardware structures through the abstract Vnode interface

## Version 0.1.3 — Procfs + Syscalls
- [x] Procfs: /proc/meminfo configured to output dynamically generated allocation reports
- [x] Procfs: /proc/[pid]/stat tracking metrics for execution times and task statistics
- [x] Procfs: Mapped /proc/self symlink logic to active process references
- [x] Implemented READDIR (16), STAT (17), DUP (18), and CHDIR (19) syscall routines
- [x] 50 Tests PASS

## Version 0.1.4 — Blocking IPC + Service Preparation
- [x] Refactored SEND_SYNC paths to transition the calling task to a BLOCKED scheduler state instead of performing busy-wait loops
- [x] Added tracking variables waiting_sender and waiting_receiver directly to the Mailbox structure
- [x] Updated send() and receive() loops to wake targeted waiting tasks explicitly
- [x] Configured destroy_mailbox() to automatically unblock and clean up all remaining queued waiting tasks
- [x] 54 Tests PASS

## Version 0.2.x — Userspace Infrastructure
*Focussing on expanding user capabilities, process spawning mechanisms, isolation layers, and robustness.*

### Version 0.2.1 — Userspace Shell + fork
- [x] FORK syscall implementation (duplicating existing task context via internal cloning structures)
- [x] WAITPID syscall tracking architecture (including custom scheduler blocking hooks)
- [x] Developed modular userspace shell (sh.c) featuring built-in commands (cd, export) alongside external binary execution
- [x] Added native pipeline redirection routing support inside the userspace shell (|, >, <)
- [x] 100 Tests PASS

### Version 0.2.2 — Pipes + Vnode Refcounting
- [x] PIPE syscall implementation (generating dual file descriptors bound to an internal ring-buffer managed within a unique Vnode wrapper)
- [x] DUP2 syscall routing mechanics (enabling file descriptor redirection alongside explicit reference count increments)
- [x] Integrated Vnode reference counting logic (refcount tracking field; defers calling close routines until active refcount == 0)
- [x] Optimized WAITPID blocking behaviors (marrying scheduler BLOCKED transitions to automated EXIT wake mechanics)
- [x] Bugfix: Correctly initialized initrd Vnode reference counters (resolved an issue where MemPool skipped clearing old heap remains)
- [x] Successfully compiled, loaded, and verified the custom userspace shell (sh.c) as an independent ELF binary
- [x] 111 Tests PASS

### Version 0.2.3 — Blocking Mechanisms
- [x] Evaluated system configuration states to accept adjustable real-time execution bounds via custom boot parameters
- [x] Developed centralized atomic kernel synchronization primitives: Semaphores, Mutexes, Queues, Task Notifications, and Event Groups
- [x] Performed full architectural review of entire core OS for vulnerabilities concerning Priority Inversion paths and Deadlocks
      — Audit completed: 4 critical priority inversion sites identified (IPC send_sync, pipe_read, tty/kbd read loops, and raw scheduler priority manipulation thresholds)
      — 3 critical deadlock vulnerabilities pinpointed (circular send_sync paths, mailbox use-after-free events, and dangling pointer risks inside pipes)
      — 7 distinct legacy ad-hoc blocking loops flagged for replacement across the tree
- [x] Replaced legacy loops with new atomic synchronization primitives where appropriate throughout the kernel
      — pipe_read/pipe_write: Replaced raw TaskControlBlock* read_waiter indicators with an internal sync::Semaphore
      — tty_read/kbd_read: Replaced primitive naked arrays with a thread-safe sync::Semaphore implementation
      — tty_wake_readers (IRQ context): Modified tracking hooks to interface safely via semaphore posts rather than manual, naked task state overrides
      — Extended devfs_init() tracking routines to safely initialize underlying hardware interface semaphores
- [x] 133 Tests PASS

### Version 0.2.4 — Stable Test Environment & Debug Engine
- [x] Fix pre-existing #GP crash (kernel_stack RFLAGS bit 1, TaskContext struct alignment, EXEC user-guard, clone safety)
- [x] Verify complete test suite execution completes stably without throwing unhandled terminal exceptions — 39/39 tests PASS
- [x] Implement a unified macro-driven Test Framework supporting customized assertions (JARVIS_TEST, JARVIS_ASSERT, JARVIS_ASSERT_HEX_EQ, JARVIS_ASSERT_EQ) producing grep-filterable logging signatures ([TEST:RUN], [TEST:PASS], [TEST:FAIL])
- [x] Implement a static, slab-safe KernelLogger subsystem that supports customizable verbosity tiers (DEBUG through FATAL) pumping out ANSI-colored strings via the serial COM1 interface
- [x] Implement a robust kernel_panic fault handler that aggregates and prints comprehensive architecture state output, capturing CPU registers (RAX, RBP, RIP, RSP, CR0–CR4) along with base stack frame tracing (RBP chain walk)
- [x] Relocate all standalone kernel-level self-tests to run within an isolated early boot stage, serving a clean shell instance to the root user task afterwards
- [x] Migrate userspace validation checking suites to run under an independent Makefile validation target (make test)
- [x] Modify Makefile configurations to output and track real-time compilation dependency files (.d)
- [x] Introduce caching layers inside build pipelines by integrating ccache options
- [x] Configure linker directives to strip out and isolate distinct debug symbol files during compilation steps (make release)
- [x] Enable Dead Code Elimination features across build profiles via optimized linker optimizations (-ffunction-sections, -fdata-sections, --gc-sections)
- [x] Add `selftest` shell command to trigger test execution interactively after boot
- [x] Comprehensive test suite: 39 tests covering string, utils, ErrorOr, PMM, MemPool, IPC, drivers, scheduler, VFS, version

### Version 0.2.5 — Priority IPC Redesign
- [ ] Embed `MessageQueue` directly in `TaskControlBlock` (remove global mailbox array)
- [ ] Add priority field to `Message` (0..31) with priority bitmap for O(1) highest-prio dequeue
- [ ] Implement priority-ordered circular buffer (`MessageQueue::push(prio)` / `MessageQueue::pop()`)
- [ ] Replace single `waiting_sender` pointer with a doubly-linked blocked-sender list via TCB
- [ ] Redesign syscall interface:
  - `send(dest, type, data, size, flags)` — block on full queue (or return -1 with `IPC_NONBLOCK`)
  - `recv(buf, max_size)` — always reads from **own** mailbox (no `src_id` parameter)
  - `call(dest, ...)` — synchronous send + block + reply
- [ ] Add O(1) task-ID→TCB lookup table in scheduler (replace global `find_mailbox()` scan)
- [ ] Implement simple priority inheritance: sender donates priority to receiver while message is queued
- [ ] Add `IPC_NONBLOCK` flag for senders that must not block
- [ ] Expose `sync::Notify` to userspace via `SYS_NOTIFY(task_id, value)` and `SYS_NOTIFY_WAIT(value*)` syscalls
- [ ] Expose `sync::EventGroup` to userspace via `SYS_EVENT_WAIT(bits, timeout)` and `SYS_EVENT_SET(task_id, bits)` syscalls
- [ ] Update all userspace programs and libc wrappers to match new syscall interface
- [ ] Update test suite for new IPC semantics
- [ ] 120 Tests PASS

### Version 0.2.6 — Exception-Safe Userspace
- [ ] Add setjmp/longjmp architectural recovery fallback blocks within copy_from_user functions to intercept invalid pointer faults gracefully
- [ ] Develop dedicated user exception handlers tracking fault conditions (#GP, #PF, #DE, #UD) triggered in Ring 3, translating failures into standard signals (SIGSEGV, SIGFPE, SIGILL) rather than forcing a full Kernel Panic
- [ ] Implement pre-termination signal dispatch loops: Enables faulted user applications to intercept standard terminations and perform vital diagnostic cleanups (closing open FDs, clearing active Mailbox locks)
- [ ] Implement SYS_KILL(pid) syscall architecture
- [ ] Implement SYS_GETPID`syscall architecture
- [ ] Establish formal process hierarchies within the system kernel, maintaining distinct parent/child relationship pointers alongside an active SYS_WAITPID tracking chain
- [ ] Ensure automatic reclamation of detached task resources (releasing physical memory blocks, closing lingering file descriptors, tearing down stale mailboxes) alongside standard process exit code propagation routines
- [ ] Enforce automated Stack Overflow detection checks by implementing hardware guard pages positioned below active user stack configurations
- [ ] Integrate an intelligent OOM-handling algorithm tasked with identifying and terminating low-priority background targets instead of panicking the entire kernel execution layer
- [ ] Restructure system call input sanitation layers by utilizing a unified CheckedPointer<T> parsing wrapper combined with robust copy_from_user and copy_to_user memory barriers
- [ ] Secure the ELF loader against malicious image formats by enforcing boundary limits on segment arrays (phdr->offset, phdr->filesz <= memsz, kernel space boundary protection)
- [ ] Integrate page table security controls inside free_user_pages via explicit memory ownership lookups using an internal allocation bitset (USER/KERNEL bit flags)
- [ ] 115 Tests PASS

### Version 0.2.7 — Userspace Signals & Syscall Extension
- [ ] Implement standard inter-process signaling system calls (SYS_SIGNAL, SYS_KILL)
- [ ] Configure automatic signal translation vectors whenever user space execution faults trigger standard exceptions (SIGSEGV, SIGFPE, SIGILL)
- [ ] Introduce dedicated execution alarm tracking options per individual thread task via SYS_ALARM
- [ ] Implement CMOS RTC driver (I/O ports 0x70/0x71) for wall-clock time with seconds
- [ ] Implement SYS_GETTOD system call tracking options (Time-of-Day clocks) backed by CMOS RTC
- [ ] Implement SYS_UNAME system call tracking options (System-wide environmental profiles)
- [ ] Add user space execution delays (sleep()) routed through the underlying task alarm timer architecture
- [ ] 120 Tests PASS

### Version 0.2.8 — Code Quality, Performance, & Migration
- [ ] Transition the core system call interface layer to evaluate operations via clean template-based type dispatch routines
- [ ] Optimize system call dispatch routines by replacing linear multi-branch switch blocks with a high-performance static function pointer table
- [ ] Migrate the legacy system call entry sequence from legacy int $0x80 software interrupts to the modern, high-speed native x86_64 syscall/sysret instruction paths using Model-Specific Registers (IA32_STAR, IA32_LSTAR, IA32_FMASK) to minimize real-time context latency
- [ ] Enforce complete Type Safety rules across the codebase: Clean out unverified raw reinterpret_cast logic from core kernel workflows
  - [ ] Implement distinct, strongly-typed abstraction wrappers isolating PhysicalAddress and VirtualAddress arithmetic variables
  - [ ] Enforce compilation-safe static_cast patterns throughout all Vnode interfaces and Driver registration routines by deploying explicit class hierarchies
  - [ ] Migrate primitive byte-buffer parsing steps (such as parsing initrd CPIO header blocks) to safe, alignment-compliant type-punning wrappers
- [ ] Develop an abstract Arch::Serial hardware class exposing standard stream write properties (replacing raw inline assembly outb port directives)
- [ ] Design a centralized unified Logger interface wrapper layer to homogenize logging statements (phasing out scattered raw debug_write operations)
- [ ] Conduct a full tree purge to eliminate all remaining raw outb assembler operations located outside of designated architecture-specific paths
- [ ] Clean up codebase consistency by replacing all hardcoded magic numbers and inline configuration strings with descriptive, type-safe enum constants
- [ ] 125 Tests PASS

### Version 0.2.9 — FAT32 Block Filesystem
- [ ] Implement ATA PIO driver for QEMU IDE (I/O ports, read/write sectors)
- [ ] Implement block device abstraction layer (read/write sector, ioctl)
- [ ] Implement FAT32 driver core:
  - MBR parsing, locating the FAT32 partition
  - FAT table read/caching, cluster chain traversal
  - Short 8.3 directory entry read/write
  - File allocation (find free cluster, update FAT)
- [ ] Create 128 MiB FAT32 disk image (`make disk.img` via `mkfs.fat`)
- [ ] Attach disk image in QEMU (`-drive file=disk.img,format=raw,if=ide`)
- [ ] Mount FAT32 partition as `/home` via VFS mount system
- [ ] Implement VFS operations on FAT32: open, read, write, close, mkdir, unlink, fstat, readdir
- [ ] Userspace shell can navigate `/home`, read/write files
- [ ] (Future) Boot kernel from FAT32 as rootfs instead of initrd
- [ ] 120 Tests PASS

### Version 0.2.10 — Shell Status Bar (UX)
- [ ] Implement `SYS_GETTOD` syscall (CMOS RTC read) for wall-clock time with seconds
- [ ] Expose per-task CPU time (executed ticks) via `/proc/[pid]/stat` or a dedicated syscall
- [ ] Compute response time of last shell command (wall-clock diff)
- [ ] Implement status-bar rendering in framebuffer terminal (inverted-colour bottom line)
- [ ] Implement status-bar rendering in serial terminal (ANSI escape codes, bottom line)
- [ ] Refresh status bar on every shell prompt: date/time, last command response time, CPU%, mem%
- [ ] Wire status bar into the shell display loop (non-intrusive, doesn't interfere with output)
- [ ] 125 Tests PASS

### Version 0.2.11 — PCI Enumeration
- [ ] Implement PCI config space access (I/O ports 0xCF8/0xCFC)
- [ ] Enumerate PCI bus 0, detect all devices (vendor ID, device ID, class code)
- [ ] Implement PCI capability list parsing (MSI, MSI-X, PM capabilities)
- [ ] Allocate and assign BAR (Base Address Register) resources
- [ ] Expose PCI device tree via /sys/pci or /proc/pci
- [ ] Integrate PCI discovery into boot-time driver initialisation sequence
- [ ] 125 Tests PASS

### Version 0.2.12 — tmpfs (Temporary Filesystem)
- [ ] Implement tmpfs: RAM-backed filesystem with dynamic page allocation
- [ ] Support standard VFS operations: open, read, write, close, mkdir, unlink, fstat, readdir
- [ ] Mount tmpfs at /tmp during boot
- [ ] Enforce per-task quota or global size limit (prevent OOM exhaustion)
- [ ] 125 Tests PASS

### Version 0.2.13 — Init System
- [ ] Design /sbin/init as a userspace ELF started as PID 1 by the kernel
- [ ] Implement service lifecycle: spawn, monitor, restart on crash
- [ ] Implement rc script or config-file-based boot sequence
- [ ] Implement auto-mount of filesystems (root, /proc, /tmp, /home)
- [ ] Redirect kernel shell launch to be spawned by init (not directly by kernel)
- [ ] 125 Tests PASS

### Version 0.2.14 — Virtio Drivers
- [ ] Implement virtio-mmio or virtio-pci transport layer (descriptor rings, queue sync)
- [ ] Implement virtio-blk driver (read/write sectors via virtqueue) as alternative to ATA PIO
- [ ] Integrate virtio-blk into FAT32 v0.2.9 as a backend option
- [ ] Implement virtio-net driver (packet send/receive via virtqueue)
- [ ] Implement minimal network stack (ARP, IP, UDP) over virtio-net
- [ ] 120 Tests PASS

---

## Version 0.3.x — Hard Real-Time: Scheduler + Timing
*Enforcing strict scheduling determinism, fine-grained timer resolutions, and Worst-Case Response Time guarantees.*

### Version 0.3.1 — High-Precision Timers + Deadline Monitoring
- [ ] Implement High Precision Event Timer (HPET) drivers, upgrading core system clock resolutions to 10 kHz (replacing the legacy 1 kHz PIT configuration)
- [ ] Introduce strict execution deadline monitoring routines tracking custom deadline_ticks boundaries accompanied by an asynchronous Overrun Callback facility
- [ ] Integrate automatic periodic task release controls that reset tracking fields (remaining_ticks) at the start of every scheduling interval
- [ ] Integrate Worst-Case Response Time (WCRT) analytical profiling tracking peak processing footprints (executed_ticks_max) across individual task nodes
- [ ] Implement a specialized scheduling statistics system call (SYS_SCHED_INFO)
- [ ] Expose dynamic real-time task profiles under /proc/sched (reporting deadlines, assigned execution budgets, and detected overrun frequency counters)
- [ ] 130 Tests PASS

### Version 0.3.2 — Budget Enforcement + Synchronization Protocols
- [ ] Enforce strict lock-hierarchy ordering rules (using static resource ID ranking metrics) to structurally eliminate nested deadlock vectors inside the priority inheritance pipeline
- [ ] Integrate hardware budget enforcement mechanisms: Trigger immediate context preemption loops whenever active tracking bounds read remaining_ticks == 0
- [ ] Implement a full Priority Inheritance Protocol (PIP) across internal IPC transaction layers
- [ ] Implement a full Priority Ceiling Protocol (PCP) across standard kernel-level locking objects
- [ ] Integrate proactive nested lock analysis loops to protect system threads against runtime deadlock vectors
- [ ] Develop an asynchronous runtime Lock Order Verification checking layer enabled during Debug operational states
- [ ] 135 Tests PASS

### Version 0.3.3 — WCET Analysis + Determinism Tuning
- [ ] Integrate automated Worst-Case Execution Time (WCET) tracking monitors calculating operational latencies across all exposed kernel system calls (measuring real-time min/max/avg bounds)
- [ ] Develop a Kernel Stack Usage Profiler task that monitors and records peak nesting depths per execution thread
- [ ] Conduct a thorough code paths review for architectural determinism, ensuring the total elimination of dynamic memory allocations inside critical I/O or interrupt processing paths
- [ ] Implement automated hardware-timed measures to compute exact system Interrupt Latency variations
- [ ] Integrate scheduling jitter tracking benchmarks to compute execution variance across real-time execution loops
- [ ] 140 Tests PASS

---

## Version 0.4.x — SMP + Multicore
*Scaling the OS architecture across symmetric multiprocessing environments while addressing core affinity rules.*

### Version 0.4.1 — APIC + Symmetric Multiprocessing Boot
- [ ] Enforce complete data isolation across multi-core systems by instantiating per-CPU GDT and TSS frames backed by absolute global interrupt spinlock barriers
- [ ] Initialize Local APIC configurations (enabling high-performance X2APIC topology layouts)
- [ ] Configure I/O APIC routing profiles to manage deterministic hardware interrupt vector sorting
- [ ] Implement multi-core Symmetric Multiprocessing (SMP) startup routines (waking secondary Application Processors via standard Startup Inter-Processor Interrupt sequences - SIPI)
- [ ] Allocate isolated architectural control structures per operational CPU core (maintaining unique PML4 page directories, dedicated kernel execution stacks, and unique TSS descriptors)
- [ ] 145 Tests PASS

### Version 0.4.2 — Per-CPU Scheduling + Load Balancing
- [ ] Redesign scheduling mechanics to utilize isolated per-CPU Run Queues, eliminating centralized global lock bottlenecks
- [ ] Integrate symmetric load-balancing thread distribution algorithms across available cores using idle-pull and work-push scheduling metrics
- [ ] Expose multi-core system thread affinity controls via dedicated system calls (SYS_SET_AFFINITY, SYS_GET_AFFINITY)
- [ ] Integrate custom cache-coloring allocator techniques to optimize data placement across shared memory lines
- [ ] Deploy specialized multi-core architectural primitives (Spinlocks and Read-Write Locks) to safeguard concurrent kernel flows
- [ ] **Real-Time SMP Verification Audit:** Re-profile, analyze, and re-verify all single-core WCET and WCRT mathematical bounds under active multi-core lock contention and bus-heavy load conditions to validate real-time predictability under SMP
- [ ] 150 Tests PASS

---

## Version 0.5.x — Integration + Certification
*System stabilization, validation benchmarks, hardening, and preparation for safety compliance.*

### Version 0.5.1 — System Integration Testing
- [ ] Deploy comprehensive system integration suites, validating complex cross-boundary flows between user space and the kernel
- [ ] Conduct a continuous 24-hour stability stress test executing real-time periodic profiles, validating that the systemic deadline overrun rate remains below < 0.01%
- [ ] Execute formal system verification benchmarks assessing fine-grained call latencies, IPC message throughput capacities, and context switch overhead metrics
- [ ] 155 Tests PASS

### Version 0.5.2 — Safety Hardening & Release Finalization
- [ ] Compose formal system documentation specifying system call execution determinism to satisfy safety-critical certification standards
- [ ] Conduct a strict security audit confirming the absolute containment of internal kernel memory pointers away from user space exposures
- [ ] Inject compiler stack cookie verification checks into kernel binaries via GCC -fstack-protector configuration flags
- [ ] Generate production-ready Release builds applying full compiler space optimization rules (-O3 -DNDEBUG, stripped symbols)
- [ ] Finalize full architectural documentation outputs (exporting full Doxygen configurations, high-level structural overviews, and comprehensive WCRT performance profiles)
- [ ] 160 Tests PASS

---

## Version 0.6.x — Watchdog + Task Monitoring
*Hardware and software watchdog tracking loops, proactive deadlock remediation, and automated fault recovery.*

### Version 0.6.1 — Hardware Watchdog Integration
- [ ] Initialize native ICH9 / HPET hardware watchdog timer circuits
- [ ] Implement specialized Watchdog Interrupt Service Routines to capture Pre-Timeout and Non-Maskable Interrupt (NMI) alerts
- [ ] Expose a dedicated watchdog reset system call (SYS_WATCHDOG_KICK) to allow active clearing of timer states
- [ ] Configure boot-time watchdog auto-activation loops to force a protective Kernel Panic if early initialization routines stall
- [ ] Engineer a programmatic software fallback routine running on legacy PIT components if the underlying hardware lacks native watchdog options
- [ ] 165 Tests PASS

### Version 0.6.2 — Per-Task Software Watchdog Layers
- [ ] Develop a per-task software watchdog subsystem enforcing strict deadline timelines where monitored tasks must register a reset notification prior to interval expiration
- [ ] Implement the SYS_WATCHDOG_CREATE(period_ms) system call allowing threads to instantiate unique local watchdog parameters
- [ ] Implement the SYS_WATCHDOG_KICK system call enabling tasks to reset their respective software watchdog instances
- [ ] Define automated watchdog fault recovery loops: Automatically force failed processes to a TERMINATED state and dispatch contextual recovery routines
- [ ] Expose live watchdog performance tracking structures under /proc/[pid]/watchdog (reporting remaining time spans, cumulative overrun instances, and timestamp metrics of the last kick)
- [ ] 170 Tests PASS

### Version 0.6.3 — Deadlock Detection & Automated Recovery
- [ ] Construct dynamic resource allocation dependency maps (Wait-For-Graphs) evaluating system status across active IPC operations and Mutex nodes
- [ ] Integrate non-blocking, timeout-driven deadlock tracking loops supervised by the underlying software watchdog infrastructure
- [ ] Implement automated fault isolation protocols: Terminate offending deadlocked threads immediately while gracefully reclaiming system resources to prevent full kernel stalls
- [ ] Log active system deadlock recovery performance figures under /proc/sched (reporting global deadlock counts and historical metrics detailing impacted process IDs)
- [ ] Implement a system health check query system call (SYS_HEALTH_STATUS)
- [ ] 175 Tests PASS

