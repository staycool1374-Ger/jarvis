# Jarvis RTOS

A real-time operating system for x86_64 architecture written in C++. Designed with hard realtime constraints, deterministic scheduling (Rate Monotonic), and userspace support.

## Features

### Current Version: 0.2.3 (Latest Stable)

#### Core System
- **x86_64 Long Mode Boot** — Multiboot2 compatible bootloader support
- **Boot Parameters** — Multiboot2 command-line parsing (`timer_hz`, `max_tasks`, `preempt`, `debug_sched`, `oom_killer`, `safe`, etc.)
- **Memory Management**:
  - Physical Memory Manager (PMM) with bitmap-based allocator
  - Virtual Memory Manager (VMM) with full 4-level page table hierarchy
  - MemPool Slab Allocator for kernel dynamic memory allocation
  - Heap Allocator with OOM handling and PMM fallback

#### Process & Task System
- **Rate Monotonic Scheduler** — Priority-based preemptive scheduling with tick-driven dispatch
- **Task States**: READY, RUNNING, BLOCKED, WAITING, TERMINATED
- **Userspace Support** — Ring 3 isolation with per-task page tables
- **Process Management**: fork(), exec(), waitpid(), getpid(), kill()

#### Blocking Mechanisms (Kernel Sync Primitives)
- **Semaphore** — Counting semaphore with priority-ordered waiter wake
- **Mutex** — Mutex with priority inheritance and recursive locking
- **Queue** — Fixed-size message queue with blocking send/receive and priority-ordered waiters
- **Task Notification** — Lightweight one-to-one notification with value transfer
- **Event Group** — Multi-bit event group with wait/try-wait and clear-on-exit

#### Interprocess Communication (IPC)
- Mailbox-based IPC with configurable message slots
- Blocking SEND, RECEIVE, SEND_SYNC operations
- Priority Inheritance Protocol support for synchronization

#### Virtual File System (VFS)
- Vnode abstraction layer with per-task file descriptor tables
- **Implemented Filesystems**:
  - Initrd (cpio newc format) — root filesystem
  - Devfs — `/dev/tty`, `/dev/null`, `/dev/console`, `/dev/kbd`
  - Procfs — `/proc/meminfo`, `/proc/[pid]/stat`, `/proc/self`

#### System Calls Implemented

| Number | Name               | Description                          |
|--------|--------------------|--------------------------------------|
| 0      | `SYS_YIELD`        | Voluntary context switch             |
| 1      | `SYS_SEND`         | Send message to mailbox              |
| 2      | `SYS_RECEIVE`      | Receive from mailbox                 |
| 3      | `SYS_SEND_SYNC`    | Synchronous send/receive with reply  |
| 4      | `SYS_PRINT`        | Print string to terminal             |
| 5      | `SYS_GET_TICKS`    | Get current timer tick count         |
| 6      | `SYS_EXIT`         | Terminate current task               |
| 7      | `SYS_CREATE_MAILBOX` | Create a new mailbox               |
| 8      | `SYS_DESTROY_MAILBOX` | Destroy an existing mailbox       |
| 9      | `SYS_OPEN`         | Open a file                          |
| 10     | `SYS_READ`         | Read from file descriptor            |
| 11     | `SYS_CLOSE`        | Close file descriptor                |
| 12     | `SYS_FSTAT`        | Get file status                      |
| 13     | `SYS_WRITE`        | Write to file descriptor             |
| 14     | `SYS_LSEEK`        | Seek file position                   |
| 15     | `SYS_IOCTL`        | Device I/O control                   |
| 16     | `SYS_READDIR`      | Read directory entry                 |
| 17     | `SYS_STAT`         | Get file status (by path)            |
| 18     | `SYS_DUP`          | Duplicate file descriptor            |
| 19     | `SYS_CHDIR`        | Change working directory             |
| 20     | `SYS_EXEC`         | Execute ELF binary (replaces task)   |
| 21     | `SYS_FORK`         | Duplicate current process            |
| 22     | `SYS_WAITPID`      | Wait for child process termination   |
| 23     | `SYS_GETPID`       | Get process ID                       |
| 24     | `SYS_KILL`         | Send signal to a process             |
| 25     | `SYS_PIPE`         | Create pipe I/O channel              |
| 26     | `SYS_DUP2`         | Redirect file descriptor             |

#### Userspace Library (libc)
- **crt0** — C runtime startup with argc/argv/envp setup
- **printf** — Formatted output (supports `%s`, `%d`, `%x`, `%c`)
- **Standard Headers**: `stdlib.h`, `stdio.h`, `string.h`, `unistd.h`, `sys/stat.h`

#### Userspace Programs
- `sh` — Interactive shell with pipeline support (`|`, `>`, `<`)
- `ls` — Directory listing
- `cat` — File content display
- `top` — Task listing utility

#### Debugging & Testing
- **Benchmark Commands**: CPU benchmark, allocator benchmark
- **Self-Test Framework**: 133+ test cases covering all kernel subsystems
- **Shell Commands**: `selftest`, `bench`, `tasks`, `meminfo`, `modlist`

## Build Requirements

### Toolchain (Linux)
```bash
# Install cross-compilation toolchain and utilities
sudo apt update && sudo apt install -y \
    build-essential git wget xorriso dosfstools grub-pc-bin \
    gcc-x86_64-linux-gnu binutils xz-utils qemu-system-x86
```

### Dependencies
- **GCC** >= 7.0 (cross-compiler for x86_64)
- **GNU Make**
- **NASM** (assembler for boot code)
- **objcopy**, **ld** from GNU Binutils
- **xorriso** (for ISO creation)
- **QEMU** (for testing/emulation)

## Build and Run

### Build the kernel and initrd
```bash
cd os
make all
```

### Run in QEMU emulator
```bash
qemu-system-x86_64 -m 512M \
    -drive format=raw,file=jarvis-rtos.iso \
    -serial mon:stdio
```

### Quick test with make
```bash
make qemu-iso    # Build and boot with GRUB in QEMU
```

## Usage Examples

### Kernel Shell Commands
```
boot> ls           List kernel structures (tasks, devices)
boot> sh           Launch userspace shell with interactive mode
boot> run program  Execute a program
boot> tasks        Show running tasks
boot> meminfo      Display memory usage
boot> version      Show kernel version
boot> modprobe keyboard   Load keyboard driver
boot> selftest     Run all self-tests
boot> bench cpu    Run CPU benchmark
```

### Userspace Shell (sh)
```
~ $ ls             List directory files
~ $ cd /path       Navigate directories
~ $ export VAR=value   Set environment variable
~ $ ./program      Run a program
~ $ cat file       Display file contents
~ $ ls | cat       Pipe stdout to stdin
~ $ cat > file     Redirect output to file
~ $ top            Show task information
```

## Architecture Overview

```
┌───────────────────────────────────────────────┐
│                  Userspace                    │
│  ┌─────────────────────────────────────────┐  │
│  │  sh  │  ls  │  cat  │  top  │  ...      │  │
│  └─────────────────────────────────────────┘  │
│  ┌─────────────────────────────────────────┐  │
│  │           libc (syscall wrappers)       │  │
│  └─────────────────────────────────────────┘  │
├───────────────────────────────────────────────┤
│                  Kernel                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │  PMM     │ │  VMM     │ │  MemPool     │  │
│  │(phys mem)│ │(virt mem)│ │ (slab alloc) │  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │Scheduler │ │  IPC     │ │  VFS (vnode) │  │
│  │  (RM)    │ │(mailbox) │ │ FS abstraction│  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │  ELF     │ │  Syscall │ │  Drivers     │  │
│  │  Loader  │ │Handler   │ │  (kbd, timer)│  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌─────────────────────────────────────────┐  │
│  │         Arch (GDT, IDT, Timer, ISR)    │  │
│  └─────────────────────────────────────────┘  │
├───────────────────────────────────────────────┤
│               Hardware (x86_64)               │
└───────────────────────────────────────────────┘
```

### Subsystem Details

- **Memory Management**: Physical pages tracked via bitmap; virtual memory uses 4-level page tables (PML4 → PDPT → PD → PT). Kernel occupies the higher half (0xFFFF800000000000+), userspace uses the lower half (< 0x0000800000000000).
- **Process Control**: Rate-monotonic scheduler selects the highest-priority ready task each tick. Tasks can be kernel-only or userspace (Ring 3 with isolated page tables).
- **IPC Communication**: Mailbox-based message passing with fixed-size messages (64 bytes data). Supports synchronous (blocking) and asynchronous patterns.
- **Filesystem**: All access through Vnode interface with dynamic dispatch. initrd provides the root filesystem; devfs and procfs are synthetic.
- **System Calls**: Via `int $0x80` with register-based argument passing. Return values propagated through the register save area.

## Development Workflow

Each version iteration follows this cycle:
1. Implement feature(s) in kernel code
2. Add unit tests to `src/kernel/test/test.cpp`
3. Build: `make all`
4. Run tests: `make test` or use `selftest` in the kernel shell
5. Verify all tests PASS
6. Build release: `make qemu-iso`
7. Update documentation (Doxygen, ROADMAP.md)

### Testing
```bash
# In the kernel shell:
selftest           # Run all registered self-tests
selftest pmm       # Run only PMM-related tests
selftest string    # Run string operation tests
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the complete version history and planned features.

Current development: **Version 0.2.4 — Stable Testenvironment**

## License

MIT License
