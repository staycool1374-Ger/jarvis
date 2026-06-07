# Jarvis RTOS

A real-time operating system for x86_64 architecture written in C++. Designed with hard realtime constraints, deterministic scheduling (Rate Monotonic), and userspace support.

## Features

### Current Version: 0.2.6 "Exception-Safe Userspace"

**New in 0.2.6:** Fixed VMM crash when GRUB video modules loaded — framebuffer at 2 GiB now maps correctly via HHDM. All 492 self-tests pass with GRUB `insmod all_video` loaded.

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
- **Fork Crash Fix**: `clone_kernel_pml4` zeroes user entries (0–255), copies kernel entries (256–511). Fork shares parent PML4 entries with `page_table_shared_` flag. `free_user_pages()` skips shared/kernel-owned pages.
- **Shell as Kernel Task**: `service::Shell::shell_task_main` runs as a kernel task at boot via `TaskControlBlock::create`
- **Demo Program**: Registered with `ProgramRegistry`, runnable via `run demo`

#### Blocking Mechanisms (Kernel Sync Primitives)
- **Semaphore** — Counting semaphore with priority-ordered waiter wake
- **Mutex** — Mutex with priority inheritance and recursive locking
- **Queue** — Fixed-size message queue with blocking send/receive and priority-ordered waiters
- **Task Notification (Notify)** — Lightweight one-to-one notification with value transfer
- **Event Group** — Multi-bit event group with wait/try-wait and clear-on-exit

#### Interprocess Communication (IPC)
- Per-TCB MessageQueue with priority bitmap and linked blocked-sender list
- O(1) dequeue via priority bitmap scan
- Priority Inheritance Protocol support for synchronization
- IPC_NONBLOCK flag for non-blocking send/receive
- Blocking SEND, RECEIVE operations
- libc wrappers in `<ipc.h>`

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
| 27     | `SYS_NOTIFY`       | Send notification value to task      |
| 28     | `SYS_NOTIFY_WAIT`  | Wait for notification value          |
| 29     | `SYS_EVENT_SET`    | Set event group bits on task         |
| 30     | `SYS_EVENT_WAIT`   | Wait for event group bits            |

#### Userspace Library (libc)
- **crt0** — C runtime startup with argc/argv/envp setup
- **printf** — Formatted output (supports `%s`, `%d`, `%x`, `%c`)
- **Standard Headers**: `stdlib.h`, `stdio.h`, `string.h`, `unistd.h`, `sys/stat.h`, `ipc.h`

#### Userspace Programs
- `sh` — Interactive shell with pipeline support (`|`, `>`, `<`)
- `ls` — Directory listing
- `cat` — File content display
- `top` — Task listing utility

#### Debugging & Testing
- **Benchmark Commands**: CPU benchmark, allocator benchmark
- **Self-Test Framework**: 492 test cases covering all kernel subsystems
- **Shell Commands**: `selftest`, `tasks`, `meminfo`, `modlist`, `version`
- **Debug Symbols**: `make debug` builds with -Og and full debug info
- **Release Build**: `make release` builds with -g -O2 and boots in QEMU with serial+framebuffer output
- **Constants Centralised**: `src/lib/constants.hpp` for all magic numbers
- **Framebuffer Terminal**: Blinking cursor, scroll support, coloured output

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
│                  Kernel (ring-0)              │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │  PMM     │ │  VMM     │ │  MemPool     │  │
│  │(phys mem)│ │(virt mem)│ │ (slab alloc) │  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │Scheduler │ │  IPC     │ │  VFS (vnode) │  │
│  │  (RM)    │ │(MessageQ)│ │ FS abstraction│  │
│  │          │ │ Notify   │ │              │  │
│  │          │ │EventGroup│ │              │  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │  ELF     │ │  Syscall │ │  Drivers     │  │
│  │  Loader  │ │Handler   │ │  (kbd, timer)│  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
│  │  Shell   │ │  Demo    │ │  Constants   │  │
│  │(kernel   │ │(kernel   │ │  (central)   │  │
│  │  task)   │ │  task)   │ │              │  │
│  └──────────┘ └──────────┘ └──────────────┘  │
│  ┌─────────────────────────────────────────┐  │
│  │         Arch (GDT, IDT, Timer, ISR)    │  │
│  └─────────────────────────────────────────┘  │
├───────────────────────────────────────────────┤
│               Hardware (x86_64)               │
└───────────────────────────────────────────────┘
```

### Subsystem Details

- **Memory Management**: Physical pages tracked via bitmap; virtual memory uses 4-level page tables (PML4 → PDPT → PD → PT). Kernel occupies the higher half (0xFFFF800000000000+), userspace uses the lower half (< 0x0000800000000000). Fork crash fixed by zeroing user PML4 entries in child and sharing kernel entries with `page_table_shared_` flag.
- **Process Control**: Rate-monotonic scheduler selects the highest-priority ready task each tick. Tasks can be kernel-only or userspace (Ring 3 with isolated page tables). The Shell runs as a kernel boot task.
- **IPC Communication**: Per-TCB `MessageQueue` with a priority bitmap for O(1) dequeue. Each message is 64 bytes. Supports blocking and non-blocking (IPC_NONBLOCK) operations. Priority inheritance prevents inversion. `SYS_NOTIFY`/`SYS_NOTIFY_WAIT` and `SYS_EVENT_SET`/`SYS_EVENT_WAIT` complement the mailbox-based IPC.
- **Filesystem**: All access through Vnode interface with dynamic dispatch. initrd provides the root filesystem; devfs and procfs are synthetic.
- **System Calls**: Via `int $0x80` with register-based argument passing. Return values propagated through the register save area.

## Development Workflow

Each version iteration follows this cycle:
1. Implement feature(s) in kernel code
2. Add unit tests to `src/kernel/test/test.cpp`
3. Build: `make all`
4. Run tests: `make test` or use `selftest` in the kernel shell
5. Verify all tests PASS (currently 416)
6. Build release: `make release` (builds with -g -O2, QEMU with serial+framebuffer)
7. Update documentation (Doxygen, ROADMAP.md)

### Testing
```bash
# In the kernel shell:
selftest           # Run all 492 registered self-tests
selftest pmm       # Run only PMM-related tests
selftest string    # Run string operation tests
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the complete version history and planned features.

Current development: **Version 0.2.6 "Exception-Safe Userspace" — VMM HHDM Fix, GRUB Video Support, 492 Tests PASS**

## License

MIT License
