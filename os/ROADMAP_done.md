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
- [x] Refactored SEND_SYNC paths to transition the calling task to a BLOCKED scheduler state instead of performing busy$
- [x] Added tracking variables waiting_sender and waiting_receiver directly to the Mailbox structure
- [x] Updated send() and receive() loops to wake targeted waiting tasks explicitly
- [x] Configured destroy_mailbox() to automatically unblock and clean up all remaining queued waiting tasks
- [x] 54 Tests PASS

## Version 0.2.x — Userspace Infrastructure
*Focussing on expanding user capabilities, process spawning mechanisms, isolation layers, and robustness.*

### Version 0.2.1 — Userspace Shell + fork
- [x] FORK syscall implementation (duplicating existing task context via internal cloning structures)
- [x] WAITPID syscall tracking architecture (including custom scheduler blocking hooks)
- [x] Developed modular userspace shell (sh.c) featuring built-in commands (cd, export) alongside external binary execu$
- [x] Added native pipeline redirection routing support inside the userspace shell (|, >, <)
- [x] 100 Tests PASS

### Version 0.2.2 — Pipes + Vnode Refcounting
- [x] PIPE syscall implementation (generating dual file descriptors bound to an internal ring-buffer managed within a u$
- [x] DUP2 syscall routing mechanics (enabling file descriptor redirection alongside explicit reference count increment$
- [x] Integrated Vnode reference counting logic (refcount tracking field; defers calling close routines until active re$
- [x] Optimized WAITPID blocking behaviors (marrying scheduler BLOCKED transitions to automated EXIT wake mechanics)
- [x] Bugfix: Correctly initialized initrd Vnode reference counters (resolved an issue where MemPool skipped clearing o$
- [x] Successfully compiled, loaded, and verified the custom userspace shell (sh.c) as an independent ELF binary
- [x] 111 Tests PASS

### Version 0.2.3 — Blocking Mechanisms
- [x] Evaluated system configuration states to accept adjustable real-time execution bounds via custom boot parameters
- [x] Developed centralized atomic kernel synchronization primitives: Semaphores, Mutexes, Queues, Task Notifications, $
- [x] Performed full architectural review of entire core OS for vulnerabilities concerning Priority Inversion paths and$
      — Audit completed: 4 critical priority inversion sites identified (IPC send_sync, pipe_read, tty/kbd read loops, $
      — 3 critical deadlock vulnerabilities pinpointed (circular send_sync paths, mailbox use-after-free events, and da$
      — 7 distinct legacy ad-hoc blocking loops flagged for replacement across the tree
- [x] Replaced legacy loops with new atomic synchronization primitives where appropriate throughout the kernel
      — pipe_read/pipe_write: Replaced raw TaskControlBlock* read_waiter indicators with an internal sync::Semaphore
      — tty_read/kbd_read: Replaced primitive naked arrays with a thread-safe sync::Semaphore implementation
      — tty_wake_readers (IRQ context): Modified tracking hooks to interface safely via semaphore posts rather than man$
      — Extended devfs_init() tracking routines to safely initialize underlying hardware interface semaphores
- [x] 133 Tests PASS

### Version 0.2.4 — Stable Test Environment & Debug Engine
- [x] Fix pre-existing #GP crash (kernel_stack RFLAGS bit 1, TaskContext struct alignment, EXEC user-guard, clone safet$
- [x] Verify complete test suite execution completes stably without throwing unhandled terminal exceptions — 39/39 test$
- [x] Implement a unified macro-driven Test Framework supporting customized assertions (JARVIS_TEST, JARVIS_ASSERT, JAR$
- [x] Implement a static, slab-safe KernelLogger subsystem that supports customizable verbosity tiers (DEBUG through FA$
- [x] Implement a robust kernel_panic fault handler that aggregates and prints comprehensive architecture state output,$
- [x] Relocate all standalone kernel-level self-tests to run within an isolated early boot stage, serving a clean shell$
- [x] Migrate userspace validation checking suites to run under an independent Makefile validation target (make test)   
- [x] Modify Makefile configurations to output and track real-time compilation dependency files (.d)
- [x] Introduce caching layers inside build pipelines by integrating ccache options
- [x] Configure linker directives to strip out and isolate distinct debug symbol files during compilation steps (make r$
- [x] Enable Dead Code Elimination features across build profiles via optimized linker optimizations (-ffunction-sectio$
- [x] Add `selftest` shell command to trigger test execution interactively after boot
- [x] Comprehensive test suite: 39 tests covering string, utils, ErrorOr, PMM, MemPool, IPC, drivers, scheduler, VFS, v$

### Version 0.2.5 — Priority IPC Redesign
- [x] Embed `MessageQueue` via pointer in `TaskControlBlock` (remove global mailbox array)
- [x] Add priority field to `Message` (0..31) with priority bitmap for O(1) highest-prio dequeue
- [x] Implement priority-ordered circular buffer (`MessageQueue::push()` / `MessageQueue::pop()` compact-remove O(n))
- [x] Replace single `waiting_sender` pointer with a blocked-sender list via TCB
- [x] Redesign syscall interface:
  - `send(dest, type, data, size, flags)` — block on full queue (or return -1 with `IPC_NONBLOCK`)
  - `recv(buf, max_size)` — always reads from **own** mailbox (no `src_id` parameter)
  - `send_sync(dest, ...)` — synchronous send + block + reply
- [x] Add O(1) task-ID→TCB lookup hash table in scheduler (replace linear scan)
- [x] Implement simple priority inheritance: sender donates priority to receiver while message is queued
- [x] Add `IPC_NONBLOCK` flag for senders that must not block
- [x] Expose `sync::Notify` to userspace via `SYS_NOTIFY(task_id, value)` and `SYS_NOTIFY_WAIT(value*)` syscalls
- [x] Expose `sync::EventGroup` to userspace via `SYS_EVENT_WAIT(bits, timeout)` and `SYS_EVENT_SET(task_id, bits)` sys$
- [x] Add userspace libc wrappers for new IPC, Notify, and EventGroup syscalls (`src/libc/ipc.h`)
- [x] Update test suite for new IPC semantics (15 new tests, 50 total)
- [x] 410 Tests PASS (all 50 kernel self-tests × pass)

### Version 0.2.6 — VMM HHDM Fix, Stable Boot with Video
- [x] Fix VMM crash when GRUB video modules loaded (framebuffer at 2 GiB now maps via HHDM)
- [x] All 492 self-tests pass with GRUB `insmod all_video` loaded

### Version 0.2.7 — Error Assertion Retrofit
- [x] ASSERT(err) / ENSURE(cond) macros + per-module error_string<E> specialisation
- [x] Module error headers: task, PMM, VMM (X-macro pattern)
- [x] Retrofitted: task, scheduler, PMM, VMM, MemPool, IPC — all ASSERT/ENSURE call sites
- [x] Retrofit sync (mutex, semaphore, queue, eventgroup, notify)
- [x] Retrofit syscall handlers (process, fs, ipc, sync, misc)
- [x] Retrofit VFS (resolve, mount, FdTable, initrd_fs, devfs, procfs, pipe)
- [x] Retrofit ELF loader, driver registry, kernel.cpp, bootparams.cpp

### Version 0.2.8 — Code Refactoring & Streamlining
- [x] O(1) syscall dispatch table, MSR-based syscall/sysret entry
- [x] Type-safe PhysicalAddress/VirtualAddress wrappers, no raw reinterpret_cast
- [x] Unified Arch::Serial + Logger, inline-asm purged from non-arch/ code
- [x] Linker GC, function/data sections, const correctness, reference params

### Version 0.2.9 — Microkernel Foundation & Full Lifecycle Audit
- [x] Privilege audit, IPC latency benchmark, unprivileged userspace shell
- [x] Task lifecycle audit: reap_orphans fix, elf::load init_task_common, blocked-sender cleanup, page_table_shared_ use-after-free
- [x] Userspace idle task (Ring 3 pause loop)
- [x] Userspace server infrastructure: VFS daemon (vfsd), I/O daemon (iocd)
- [x] Capability-based MMIO access control
- [x] Wait-for-graph deadlock detection engine (WFG)
- [x] Deadlock recovery: terminate task, release locks, unblock waiters
- [x] Health status syscall and /proc/health metrics
- [x] Comprehensive full audit: bugs #009-#019 closed (7 fixed, 1 WONTFIX)
- [x] 1233 kernel self-tests PASS, all release tests PASS
