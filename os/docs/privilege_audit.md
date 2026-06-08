# Kernel Subsystem Privilege Audit (v0.2.9)

## Legend
- **Ring**: Current privilege level (0 = kernel, 3 = user)
- **HW Access**: Hardware resources used (I/O ports, MSRs, interrupts, MMIO, special instructions)
- **Can move to R3?**: Whether the subsystem could theoretically run in userspace
- **Dependencies**: Other subsystems it calls into

---

## 1. Architecture (arch/)

### 1.1 GDT (gdt.cpp/hpp)
- **Ring**: 0
- **HW Access**: `lgdt` instruction, MSRs (for TSS, FS/GS base)
- **Can move to R3?**: NO — GDT is a ring-0-only structure
- **Dependencies**: None

### 1.2 IDT (idt.cpp/hpp)
- **Ring**: 0
- **HW Access**: `lidt` instruction
- **Can move to R3?**: NO — IDT is ring-0-only
- **Dependencies**: None

### 1.3 ISR Stubs (isr_stubs.asm)
- **Ring**: 0
- **HW Access**: Exception vectors, interrupt gate entries
- **Can move to R3?**: NO — exception/interrupt handling requires ring 0
- **Dependencies**: GDT, IDT

### 1.4 Timer / PIT (timer.cpp/hpp)
- **Ring**: 0
- **HW Access**: PIT I/O ports (0x40-0x43), IRQ0
- **Can move to R3?**: Partially — PIT programming could be done from ring 3 if I/O ports were granted via TSS I/O bitmap, but the interrupt handler must stay in ring 0.
- **Dependencies**: IDT (IRQ registration), io.hpp

### 1.5 RTC (rtc.cpp/hpp)
- **Ring**: 0
- **HW Access**: CMOS/RTC I/O ports (0x70-0x71), IRQ8
- **Can move to R3?**: Partially — same as PIT; read-only RTC access could be via syscall.
- **Dependencies**: IDT, io.hpp

### 1.6 Keyboard (keyboard.cpp/hpp)
- **Ring**: Currently 0 (PS/2 I/O ports 0x60/0x64, IRQ1)
- **HW Access**: I/O ports 0x60 (data), 0x64 (status/cmd), IRQ1
- **Can move to R3?**: Requires careful design. The IRQ handler must stay in ring 0 (or be forwarded via IPC to a userspace driver). I/O port access could be granted via TSS I/O bitmap if IOMMU-like port filtering were available. **Candidate for /sbin/iocd**
- **Dependencies**: IDT (IRQ1), io.hpp, ring buffer data structure

### 1.7 Serial (serial.hpp)
- **Ring**: 0 (accesses COM1 at 0x3F8)
- **HW Access**: I/O ports 0x3F8-0x3FF
- **Can move to R3?**: Same as keyboard — requires I/O port access. **Candidate for /sbin/iocd**
- **Dependencies**: io.hpp

### 1.8 MSR (msr.hpp)
- **Ring**: 0
- **HW Access**: `rdmsr`/`wrmsr` instructions
- **Can move to R3?**: NO — MSR access is ring-0-only

### 1.9 I/O wrappers (io.hpp, io_asm.asm)
- **Ring**: 0
- **HW Access**: `in`/`out` instructions, CR0/CR2/CR3/CR4, `hlt`, `pause`, `cli`/`sti`, `rdtsc`
- **Can move to R3?**: NO — control registers, CLI/STI, and HLT are ring-0-only (except HLT in ring 3 if CPL=3 with appropriate IOPL)

---

## 2. Memory Management (memory/)

### 2.1 PMM — Physical Memory Manager (pmm.cpp/hpp)
- **Ring**: 0
- **HW Access**: EFI memory map (from boot params), physical page manipulation
- **Can move to R3?**: NO — physical memory management requires ring 0
- **Dependencies**: bootparams.hpp, address.hpp

### 2.2 VMM — Virtual Memory Manager (vmm.cpp/hpp)
- **Ring**: 0
- **HW Access**: Page table manipulation (CR3, INVLPG), HHDM direct physical mapping
- **Can move to R3?**: NO — page table management is ring-0-only
- **Dependencies**: PMM, address.hpp

### 2.3 MemPool (mempool.cpp/hpp)
- **Ring**: 0
- **HW Access**: None (uses PMM-allocated pages internally)
- **Can move to R3?**: A userspace heap allocator exists in libc; this kernel heap stays in ring 0.
- **Dependencies**: PMM

### 2.4 Address wrappers (address.hpp)
- **Ring**: 0
- **HW Access**: None (pure computation)
- **Can move to R3?**: Could be shared as a utility header
- **Dependencies**: None

### 2.5 CheckedPtr (checked_ptr.hpp)
- **Ring**: 0
- **HW Access**: None
- **Can move to R3?**: Kernel-only (validates user/kernel address boundaries)
- **Dependencies**: None

---

## 3. Task Management & Scheduling (task/)

### 3.1 TaskControlBlock (task.cpp/hpp)
- **Ring**: 0
- **HW Access**: Stack allocation (PMM), page table cloning (VMM), context switch state
- **Can move to R3?**: NO — task creation/destruction requires ring 0
- **Dependencies**: PMM, VMM, vfs (fd_table), ipc (msg_queue)

### 3.2 Scheduler (scheduler.cpp/hpp)
- **Ring**: 0
- **HW Access**: Context switching (CR3, RSP0 via TSS), timer ticks
- **Can move to R3?**: NO — context switching is inherently ring 0
- **Dependencies**: TaskControlBlock, GDT (TSS), VMM, Timer

---

## 4. IPC (ipc/)

### 4.1 Message Queue & IPC (ipc.cpp/hpp)
- **Ring**: 0
- **HW Access**: None (pure data structures with scheduler interaction)
- **Can move to R3?**: The data structures (Message, MessageQueue) could be shared. The IPC::send/recv functions that interact with the scheduler (block_sender, wake_sender) must stay in ring 0 because they manipulate task states.
- **Dependencies**: Scheduler, TaskControlBlock

---

## 5. Synchronization (sync/)

### 5.1 Notify, EventGroup, Mutex, Semaphore, Queue
- **Ring**: 0
- **HW Access**: None (wait/notify patterns, scheduler integration)
- **Can move to R3?**: The core primitives could be exposed to userspace via syscalls (already done for Notify and EventGroup). Kernel-internal use stays in ring 0.
- **Dependencies**: Scheduler, TaskControlBlock

---

## 6. Virtual File System (vfs/)

### 6.1 VFS Core (vfs.cpp/hpp)
- **Ring**: 0
- **HW Access**: None (pure data structure management: Vnodes, mounts, path resolution)
- **Can move to R3?**: YES — VNode path resolution, mount table, and fd table management are pure computation with no hardware access. **Candidate for /sbin/vfsd**
- **Dependencies**: TaskControlBlock (cwd, fd_table), PMM (allocations)

### 6.2 initrd_fs (initrd_fs.cpp/hpp)
- **Ring**: 0
- **HW Access**: Reads from embedded initrd buffer (physical memory at known address)
- **Can move to R3?**: Partially — if the initrd buffer is mapped into the server's address space, it could serve file contents via IPC. Requires the kernel to expose a `map_physical` syscall.
- **Dependencies**: VnodeOps definitions, initrd memory region

### 6.3 devfs (devfs.cpp/hpp)
- **Ring**: 0
- **HW Access**: I/O ports (COM1 via tty_read/tty_write), keyboard ring buffer (kbd_read)
- **Can move to R3?**: The inode/entry management could move, but the read/write implementations directly access hardware. **Split:** directory structure moves to vfsd; device I/O stays in kernel or moves to iocd.
- **Dependencies**: io.hpp (port I/O), Keyboard, Terminal

### 6.4 procfs (procfs.cpp/hpp)
- **Ring**: 0
- **HW Access**: Reads kernel data structures (Scheduler, PMM)
- **Can move to R3?**: YES — if kernel exposes a syscall for reading task/memory stats. **Candidate for /sbin/vfsd** with kernel stats via syscall.
- **Dependencies**: Scheduler, PMM

### 6.5 Pipe (pipe.cpp/hpp)
- **Ring**: 0
- **HW Access**: None (circular buffer with semaphore)
- **Can move to R3?**: Partially — the buffering could be in userspace, but the synchronization (blocking on read/write) requires scheduler interaction. Could be implemented with a ring buffer + notification syscalls.
- **Dependencies**: Semaphore, VnodeOps

---

## 7. ELF Loader (elf/)

### 7.1 ELF Loader (elf.cpp/hpp)
- **Ring**: 0
- **HW Access**: Page table manipulation (VMM), PMM allocation
- **Can move to R3?**: NO — ELF loading maps pages into the target process's address space, which requires ring 0.
- **Dependencies**: VMM, PMM

---

## 8. Syscall Layer (syscall/)

### 8.1 Syscall Entry (syscall_entry.asm)
- **Ring**: 0 (entry), handles user→kernel transitions
- **HW Access**: `syscall`/`sysret`, `swapgs`, MSRs (STAR/LSTAR/FMASK)
- **Can move to R3?**: NO — the syscall entry point is fundamentally ring-0

### 8.2 Syscall Dispatch & Handlers (syscall.cpp/hpp, handlers_*.cpp)
- **Ring**: 0
- **HW Access**: None (dispatch table, argument validation)
- **Can move to R3?**: NO — these run in ring 0 as part of syscall handling.
- **Dependencies**: All kernel subsystems

---

## 9. Services (services/)

### 9.1 Shell (shell.cpp/hpp)
- **Ring**: Currently 0 (kernel task)
- **HW Access**: Reads keyboard directly (via Keyboard::getchar), reads serial directly (via COM1 LSR), uses Terminal::write for output
- **Can move to R3?**: YES — the userspace shell (`userspace/sh.c`) already exists and runs in ring 3 via syscalls. It already uses `read(0, ...)` and `write(1, ...)` through the VFS (which routes to devfs → tty/kbd). **Candidate for unprivileged shell.**
- **Dependencies**: Terminal, Keyboard, Scheduler, VFS, ELF loader, PMM, Timer

### 9.2 Terminal (terminal/framebuffer.cpp, terminal.cpp, terminal.hpp)
- **Ring**: 0
- **HW Access**: Framebuffer MMIO (from Multiboot2), serial port I/O
- **Can move to R3?**: Partially — the framebuffer could be mapped into a userspace terminal server's address space. The TTY read/write path already goes through VFS, so a userspace sh→VFS→kernel path already works. The framebuffer manipulation itself could be in ring 3 if the framebuffer physical memory is mapped into the server's page table.
- **Dependencies**: io.hpp (serial), VMM (framebuffer mapping)

### 9.3 Program Registry (program.cpp/hpp)
- **Ring**: 0
- **HW Access**: None
- **Can move to R3?**: YES — just a list of in-kernel demo programs. Could be userspace ELFs instead.
- **Dependencies**: None

---

## 10. Drivers (driver/)

### 10.1 Driver Registry (driver.cpp/hpp)
- **Ring**: 0
- **HW Access**: None (just a registry table for driver names)
- **Can move to R3?**: YES — could be a userspace service that tracks available drivers. Currently only used for self-test verification.
- **Dependencies**: None

---

## 11. Libraries (lib/)

### 11.1 Logger (logger.cpp/hpp)
- **Ring**: 0 (but conceptually could be shared)
- **HW Access**: Serial port (via arch::Serial)
- **Can move to R3?**: YES — if serial output is available via syscall (already is via write(1, ...)). A userspace logger would just write to stdout.
- **Dependencies**: arch::Serial

### 11.2 Version (version.cpp/hpp)
- **Ring**: 0
- **HW Access**: None
- **Can move to R3?**: YES — already exposed via sys_uname (syscall 35)
- **Dependencies**: None

### 11.3 Test Framework (test.cpp/hpp)
- **Ring**: 0
- **HW Access**: None
- **Can move to R3?**: YES — the JARVIS_TEST macros work in any context
- **Dependencies**: Logger

---

## Summary: Migration Candidates

| Subsystem | Priority | Effort | Server Name | Key Challenge |
|-----------|----------|--------|-------------|---------------|
| **Shell** | HIGH | Small | (userspace sh.c) | Already exists as userspace ELF; needs to be auto-started |
| **VFS Core** | HIGH | Large | `/sbin/vfsd` | Moving Vnode resolution, mounts, fd tables to separate process |
| **initrd_fs** | HIGH | Medium | `/sbin/vfsd` | Needs physical memory mapping for initrd buffer |
| **procfs** | HIGH | Medium | `/sbin/vfsd` | Needs kernel stats syscall |
| **devfs** | HIGH | Large | Split | Directory structure in vfsd, device I/O in kernel or iocd |
| **Keyboard** | MEDIUM | Large | `/sbin/iocd` | IRQ forwarding from kernel to userspace |
| **Serial** | MEDIUM | Medium | `/sbin/iocd` | I/O port access from userspace |
| **Timer/RTC** | LOW | Medium | `/sbin/iocd` | IRQ handling must stay in kernel; read-only via syscall |

## Recommended Order

1. ✅ **IPC Latency Benchmark** — measure current baseline
2. **Unprivileged Shell** — simplest migration, already exists
3. **Capability-based Hardware Access** — foundation for driver isolation
4. **User-space VFS Server** — most impactful migration
5. **User-space Driver Server** — requires IRQ and I/O port mechanisms
