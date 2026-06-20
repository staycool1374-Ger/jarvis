# Jarvis RTOS

A real-time operating system for the `x86_64` architecture, written from scratch in
freestanding C++20. Designed for ISO 26262 ASIL D safety standards, deterministic
scheduling, and clean kernel/userspace separation. All services run in a monolithic
kernel at Ring 0 with userspace processes in Ring 3.

**Current Version:** v0.2.15 — Phase 3: Hardware Enablement

---

## Features

### Core System & Memory Management

- **x86_64 Long Mode Boot** — Multiboot2-compliant bootloader, 4-level page tables.
- **Physical Memory Manager (PMM)** — Buddy allocator with O(1) page frame allocation and coalescing.
- **Virtual Memory Manager (VMM)** — Full 4-level page table hierarchy, kernel/user space separation, page cloning for `fork()`.
- **Slab Allocator (MemPool)** — Deterministic fixed-size object caches for kernel allocations.
- **Heap Allocator** — Dynamic kernel heap with OOM fallback.
- **Memory Integrity** — Canary markers, stack markers in dedicated sections, bounds checking.
- **Gcov Support** — Code coverage instrumentation for debug builds.

### Process & Task Management

- **Rate Monotonic Scheduler (RMS)** — Fixed-priority preemptive scheduling with 256 priority levels.
- **Task States** — `READY`, `RUNNING`, `BLOCKED`, `WAITING`, `TERMINATED`.
- **Userspace Isolation** — Ring 3 execution with per-task page tables, user stacks, and TSS SS0/RSP0 for syscall entry.
- **Process Lifecycle** — `fork()` with copy-on-write page cloning, `waitpid()`, `exit()`, `exec()`.
- **ELF Loader** — Parses and loads statically linked ELF64 binaries from initrd or VFS.
- **Resource Limits** — Per-task RLIMIT tracking (`SYS_GETRLIMIT`/`SYS_SETRLIMIT`).
- **Secure Execution** — `CheckedPointer` validation for kernel pointer safety.
- **Task Notifications** — Lightweight 32-bit value signaling, event groups with multi-bit wait.

### Synchronisation Primitives

- **Mutex** — Blocking mutual exclusion with priority inheritance.
- **Semaphore** — Counting semaphore with blocking wait.
- **Event Group** — Multi-bit event synchronisation (`SYS_EVENT_SET`/`SYS_EVENT_WAIT`).
- **Queue** — Fixed-size message queue (kernel space).
- **Notify** — Per-task notification value with wait/clear semantics.
- **Wait-for-Graph** — Deadlock detection engine with cycle detection.
- **IrqGuard** — RAII interrupt disable guard for critical sections.

### Interprocess Communication (IPC)

- Priority-aware message queue with per-TCB mailbox and priority bitmap.
- IPC primitives: `SYS_SEND`, `SYS_RECEIVE`, `SYS_SEND_SYNC`, `IPC_NONBLOCK`.
- Priority inheritance on IPC blocks prevents priority inversion.
- Linked blocked-sender list for O(1) sender unblocking.
- Buffer pool for zero-copy message payload transfer.
- Dynamic mailbox creation/destruction.
- Pipes — unidirectional byte-stream IPC (`SYS_PIPE`/`SYS_DUP2`).

### Virtual File System (VFS)

- Unified `vnode` abstraction with operation vtable (`read`, `write`, `open`, `close`, `readdir`, `lookup`, `fstat`, `mkdir`, `unlink`, `ioctl`).
- Per-task file descriptor tables tracking standard I/O and custom files.
- Mount table with path-based lookup.
- **Mounted Filesystems:**
  - **Initrd** — POSIX cpio (newc) archive mounted at boot as rootfs.
  - **Devfs** — Virtual device tree: `/dev/tty`, `/dev/null`, `/dev/console`, `/dev/kbd`, `/dev/fb`.
  - **Procfs** — Kernel metrics: `/proc/meminfo`, `/proc/[pid]/stat`, `/proc/self`, `/proc/sched`.
  - **FAT32** — Full read/write support: open, read, write, close, readdir, mkdir, unlink, fstat.
  - **FAT32 Image** — Pre-built disk image mounted automatically at `/fat/`.
- VFS Daemon — authorises and proxies filesystem operations for userspace tasks.

### Kernel Shell (36+ Built-in Commands)

A full-featured interactive shell running as a kernel task with serial and PS/2 keyboard input,
framebuffer terminal output, status bar, and color-coded prompt.

| Category | Commands |
|----------|----------|
| Navigation | `cd`, `pwd`, `ls`, `dirs`, `pushd`, `popd` |
| File ops | `mkdir`, `rm`, `rmdir`, `cat` |
| System info | `help`, `uptime`, `version`, `meminfo`, `tasks`, `jobs`, `env`, `times` |
| Process control | `run`, `runelf`, `exit`/`shutdown`, `reboot`, `sleep`, `wait`, `fg`, `bg`, `disown` |
| Shell built-ins | `alias`/`unalias`, `history`, `type`, `which`/`locate`, `source`/`.` , `set`, `read`, `shift`, `export`, `echo`, `printf`, `test`/`[` |
| System control | `modprobe`, `modlist`, `selftest`, `clear`, `trap`, `umask`, `ulimit` |
| Features | Output redirection (`>`), alias expansion, command history (100 entries, ring buffer), directory stack, 32 environment variables, shell options (`-x`, `-e`, `-u`) |

### System Calls (int 0x82, 44 syscalls)

| # | Syscall | Description |
|---|---------|-------------|
| 0 | `SYS_YIELD` | Voluntary context switch |
| 1 | `SYS_SEND` | Async message send to mailbox |
| 2 | `SYS_RECEIVE` | Blocking message receive from mailbox |
| 3 | `SYS_SEND_SYNC` | Synchronous rendezvous send/receive |
| 4 | `SYS_PRINT` | Print string to terminal |
| 5 | `SYS_GET_TICKS` | Uptime in timer ticks |
| 6 | `SYS_EXIT` | Terminate calling process |
| 7 | `SYS_MAILBOX_CREATE` | Create IPC mailbox |
| 8 | `SYS_MAILBOX_DESTROY` | Destroy IPC mailbox |
| 9 | `SYS_OPEN` | Open file by path |
| 10 | `SYS_READ` | Read from file descriptor |
| 11 | `SYS_CLOSE` | Close file descriptor |
| 12 | `SYS_FSTAT` | Get file descriptor status |
| 13 | `SYS_WRITE` | Write to file descriptor |
| 14 | `SYS_LSEEK` | Reposition file offset |
| 15 | `SYS_IOCTL` | Device I/O control |
| 16 | `SYS_READDIR` | Read directory entries |
| 17 | `SYS_STAT` | Get file status by path |
| 18 | `SYS_DUP` | Duplicate file descriptor |
| 19 | `SYS_CHDIR` | Change working directory |
| 20 | `SYS_EXEC` | Execute ELF binary |
| 21 | `SYS_FORK` | Clone calling process |
| 22 | `SYS_WAITPID` | Wait for child process |
| 23 | `SYS_GETPID` | Get current task PID |
| 24 | `SYS_KILL` | Send signal to task |
| 25 | `SYS_PIPE` | Create pipe |
| 26 | `SYS_DUP2` | Redirect file descriptor |
| 27 | `SYS_NOTIFY` | Send notification value |
| 28 | `SYS_NOTIFY_WAIT` | Wait for notification |
| 29 | `SYS_EVENT_SET` | Set event group bits |
| 30 | `SYS_EVENT_WAIT` | Wait for event group bits |
| 31 | `SYS_ALARM` | Schedule alarm signal |
| 32 | `SYS_SLEEP` | Sleep for N milliseconds |
| 33 | `SYS_SEM_CREATE` | Create counting semaphore |
| 34 | `SYS_SEM_WAIT` | Wait on semaphore |
| 35 | `SYS_SEM_POST` | Signal semaphore |
| 36 | `SYS_MUTEX_CREATE` | Create mutex |
| 37 | `SYS_MUTEX_LOCK` | Lock mutex |
| 38 | `SYS_MUTEX_UNLOCK` | Unlock mutex |
| 39 | `SYS_SCHED_INFO` | Query scheduler statistics |
| 40 | `SYS_SIGNAL_RETURN` | Return from signal handler |
| 41 | `SYS_MKDIR` | Create directory |
| 42 | `SYS_UNLINK` | Remove file |
| 43 | `SYS_RMDIR` | Remove empty directory |

### Hardware Support

- **Serial (COM1)** — 115200 baud, interrupt-driven I/O.
- **PS/2 Keyboard** — scancode set 2 translation, modifier key tracking.
- **Framebuffer** — Linear framebuffer via Multiboot2, terminal with status bar.
- **PIT** — Programmable interval timer, 1 kHz scheduling tick.
- **RTC** — CMOS real-time clock for wall clock display in status bar.
- **ACPI** — QEMU shutdown port for clean power-off.
- **ATA PIO** — Parallel ATA driver (read/write via PIO, block device layer).
- **IO Controller (IOCD)** — Block device I/O request dispatch queue.

### Driver Framework

- `DriverRegistry` — Central registry with `load()`/`find()`/`count()`/`get()`.
- `BlockDevice` — Abstract base with `read_sectors`/`write_sectors`.
- `MockBlockDevice` — RAM-backed block device for testing.
- Shell commands: `modprobe`, `modlist`.

### Debugging & Diagnostics

- **Self-Test Framework** — 550+ test cases in debug build, 84 in release.
- **Test Categories:** task lifecycle, IPC, memory (PMM/VMM), VFS, FAT32, scheduler, signals, pipes, ELF, drivers, serial, keyboard, RTC, integrity, security, stress, benchmarks.
- **QEMU Integration** — Full test suite runs in QEMU with serial output capture.
- **Gcov** — Code coverage instrumentation for debug builds.
- **ResourceTracker** — Tracks allocation counts during tests to detect leaks.
- **Serial Console** — Full interactive shell over serial + keyboard.
- **Status Bar** — Live display of time, uptime, memory usage, task count.

---

## Architecture

```
  [ Userspace Apps ] <--- Ring 3 Isolation
------------- [ Syscall Interface: int 0x82 ] -------------
  [ Shell (Kernel Task, 36+ Built-ins)  ]  [ RMS Scheduler       ]
  [ VFS / Initrd / Devfs / Procfs / FAT32]  [ Priority IPC Mailbox]
  [ Virtual Memory (VMM, 4-level PT)    ]  [ Notify & Event Groups]
  [ O(1) PID->TCB Hash Table            ]  [ Priority Inheritance ]
  [ Physical Memory (PMM, Buddy Alloc)  ]  [ Slab Alloc (MemPool) ]
  [ Hardware: Serial, KBD, Framebuffer,  ]  [ ATA PIO, PIT, RTC   ]
  [ Gcov, Driver Registry, Integrity    ]  [ Deadlock Detection   ]
============= Monolithic Kernel (Ring 0) =============
```

---

## Build & Run

### Prerequisites

```bash
sudo apt install build-essential git wget xorriso dosfstools \
    x86_64-linux-gnu-gcc binutils qemu-system-x86
```

### Build

```bash
git clone <repo-url>
cd os
make debug        # Debug build with test suite
make release      # Release build (optimised, no tests)
```

### Run in QEMU

```bash
make qemu-iso     # Build + launch ISO in QEMU with serial console
```

### Run Tests

Tests run automatically at boot in debug builds. To run the full suite:

```bash
make debug        # Build debug ISO
make test-qemu    # Launch QEMU, capture serial output, validate results
```

---

## License

MIT License
