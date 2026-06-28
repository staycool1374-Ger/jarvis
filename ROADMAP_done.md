# Completed Roadmap Items

## 0.2.13 — Shell UX & Utilities

### Persistent status bar + dynamic prompt
- Framebuffer driver, text terminal with scrolling, cursor blink
- Persistent 2-row status bar at screen bottom (version, date/time, uptime, mem, task count)
- Zsh-like dynamic prompt: `✓ /path $ ` — green checkmark (exit 0) / red X (non-zero), blue cwd, white `$ `

### Built-in commands
- `help`, `clear`, `echo`, `pwd`, `which`/`locate`, `env`, `sleep`
- `export VAR=value` with persistent environment storage (32 slots, 256 B each)
- Plus many pre-existing commands: uptime, tasks, meminfo, version, reboot, run, jobs, cd, modprobe, modlist, listprog, runelf, exit/shutdown, selftest

### SYS_MKDIR / SYS_UNLINK + initrd utilities
- `mkdir` and `unlink` function pointers in `VnodeOps` struct (all 16 ops tables updated)
- FAT32 write primitives: `write_fat_entry`, `write_cluster`, `clear_cluster`, `find_free_cluster`, `alloc_cluster`, `free_cluster_chain`, `name_to_short_name`, `add_dir_entry`, `remove_dir_entry`
- FAT32 `mkdir`: allocates cluster, writes `.` / `..` entries, adds parent entry
- FAT32 `unlink`: frees cluster chain, removes entry, enforces empty-dir check
- `vfs::mkdir()` / `vfs::unlink()` path-split wrappers
- Syscalls `SYS_MKDIR=41`, `SYS_UNLINK=42`, `SYS_RMDIR=43` with vfsd authorization
- `mkdir()` / `unlink()` libc wrappers
- Kernel shell: `mkdir`, `rm`, `rmdir` built-in commands
- Userspace: `mkdir.c` / `rm.c` utilities (auto-built into initrd)

### IPC pipeline hardening (kernel shell)
- Terminal output capture mechanism (`capture_begin`/`capture_end`) for `>` redirect
- `>` redirect parsing in `parse_and_exec`: captures command output and writes to file via VFS
- Works with any shell command (output visible on screen AND saved to file)
- Userspace shell (`sh.c`) already had `|`, `<`, `>` via fork/pipe/dup2
- Kernel pipe infrastructure, `sys_pipe`, `sys_dup2`, and 6 pipe tests pre-existing

### Scheduler Stabilization & Synchronization Hardening

#### RAII IrqGuard
- `src/kernel/arch/irq_guard.hpp`: stack-bound RAII class, saves RFLAGS.IF on construction, restores on destruction
- Safe across context switches (each task owns its kernel stack; IF saved/restored by `switch_to_task`)
- Nested guards work correctly (inner saves IF=0, outer does real `sti`)
- Retrofitted into `Scheduler::add_task/remove_task/reschedule`, all 28 state-mutating sync primitive methods, and 3 open-coded cli/sti sites

#### Sync primitive race condition fixes
- **R1 (check-then-act race):** Added `IrqGuard` to all state-mutating methods across Mutex, Semaphore, Notify, EventGroup, Queue
- **R2 (silent waiter overflow):** `ENSURE(added)` after `add_waiter()` in Mutex, Semaphore, EventGroup (hard panic on overflow)
- Queue `send()`/`receive()` retains original silent-overflow fallback (while-loop exceeds MAX_WAITERS in test harness without real context switch)

#### C++20 concept constraints
- `src/lib/concepts.hpp`: `Integral`, `TriviallyCopiable`, `ValueType`, `ErrorEnum`, `Lockable`
- Retrofitted 6 template sites: `align_up`/`align_down` (`Integral`), `CheckedPtr`/`checked<>`/`safe_copy_from_user`/`safe_copy_to_user` (`TriviallyCopiable`), `error_string` primary template (`ErrorEnum`)

#### Thread-safety attributes (Phase C)
- **Deferred.** Risk of false positives on ISR paths (IF=0 by hardware), unknown warning count, `[[gnu::capability]]` annotations don't map cleanly to task-parameterized `lock(TaskControlBlock*)` pattern
- Implementation guide documented in `REFACTORING-implementation.md`

### Additional 0.2.13 items
- FAT32 unlink empty-dir fix (skip `.` and `..`)
- `vfs_unlink_file`, `vfs_mkdir_valid` test isolation
- Shell `mkdir` bypasses VFS daemon for absolute paths
- BUGS.md #007 (idle task test output) — Fixed
- 28 new shell built-in commands: `alias`, `unalias`, `history`, `type`, `source` (`.`), `set`, `read`, `printf`, `test` (`[`), `shift`, `trap`, `wait`, `fg`, `bg`, `disown`, `ulimit`, `umask`, `times`, `logout`, `dirs`, `pushd`, `popd`, `ls`
- Alias expansion + command history recording in `parse_and_exec`
- Release tag: v0.2.13

### 0.2.18 — Observability & Portability
- [x] Kernel log ring buffer (SYS_KLOG, dmesg), HAL abstraction, arch/x86_64/ migration
- [x] Multi-arch build (ARCH variable), secure exec (CheckedPointer), regression audit
- [x] PCI bus enumeration / device tree debug output (pci_print_tree, sysfs /proc/pci)

### 0.2.19 — Kernel Memory Safety
- [x] Audit existing `new`/`delete` usages in kernel code for consistency with the RAII pattern
- [x] Renode simulation setup — integrate Renode as a secondary emulation platform alongside QEMU for early architectural bring-up of ARM Cortex-A (aarch64) and RISC-V (RV64) targets, enabling HAL validation and cross-architecture testing before hardware is available

### 0.2.16 — CPU Features & RNG
- Lazy FPU/SSE context switch (FXSAVE/FXRSTOR)
- Hardware RNG (RDRAND/RDSEED) + ChaCha20 PRNG → /dev/random, SYS_GETRANDOM
- Release tag: v0.2.16

### 0.2.17 — Kernel Synchronization & Real-Time Guarantees
- Phase 1: SpinLock primitive + RAII guards (`SpinLock`, `SpinLockGuard<Lock>`, preemption-aware CAS + `arch::pause()`)
- Phase 2: Migrate sync primitives (Mutex, Semaphore, Queue, Notify, EventGroup) from IrqGuard to per-object SpinLock
- Phase 3a: Migrate Scheduler (add_task, remove_task, reschedule) to static SpinLock
- Phase 3b: Volatile→Atomic context-switch globals (C bridge for isr_stubs.asm)
- Phase 4: Migrate VFS tmpfs to sleepable per-filesystem Mutex
- Phase 5: Lock-free SPSC ring buffer for ISR→task handoff, keyboard ISR migration
- Phase 6: Lock-free IPC receive/send (replace sti;hlt;cli with SpinLock + BLOCKED state)
- Phase 7: Remaining IrqGuard sites in sys_brk and shell::cmd_selftest
- Phase 8: Validation & benchmarks (8 test suites, 2 benchmark suites)
- Release tag: v0.2.17

## Phase 3: System Services & Hardware (v0.12.14–v0.2.25)

### v0.12.14 — System Services
- tmpfs (/tmp, user quotas), init system (PID 1, /etc/rc), fstab automount
- SYS_GETRLIMIT/SYS_SETRLIMIT, SYS_BRK, text pager/editor utilities
- IrqGuard enforcement in all tmpfs operations and sys_brk

### 0.2.15 — Hardware Enablement
- PCI enumeration — CF8/CFC config space access, bus scan, BAR parsing, PCI bridge support
- MSI/MSI-X interrupt support — capability detection, vector allocator, MSI/MSI-X enable
- Virtio transport (modern 1.0 PCI) + block driver — capability parsing, MMIO mapping, feature negotiation, queue setup, block I/O
- DMA driver — physically contiguous buffer alloc, scatter-gather list, PRD table (ATA bus-master format), PCI bus master control
- Minimal network stack — Ethernet/ARP/IPv4/UDP protocol types, ARP cache with resolution, IPv4 header checksum, UDP send/receive, virtio-net NIC driver (modern 1.0 PCI)

### 0.2.20 — System Calls & Storage
- [x] SYS_YIELD — cooperative task yielding for CPU-bound tasks
- [x] SYS_REBOOT / SYS_HALT — system power management from userspace
- [x] AHCI/SATA driver with NCQ (replaces ATA PIO for bare-metal storage)
- [x] DMA completion interrupt infrastructure (ISR acknowledges and fires for storage I/O)
- [x] Double-buffered DMA transfer support (ping-pong buffers for streaming storage)
- Release tag: v0.2.20

### 0.2.21 — Kernel Configuration & Portability
- [x] jarvis_config.h central configuration header with 60+ CONFIG_* defines
- [x] Scheduling tunables migrated: CONFIG_MAX_TASKS, CONFIG_TICK_HZ, CONFIG_PRIORITY_CEILING, CONFIG_PREEMPTION, CONFIG_IDLE_YIELD, CONFIG_TIME_SLICING
- [x] Memory layout tunables migrated: CONFIG_PAGE_SIZE, CONFIG_HHDM_OFFSET, CONFIG_PML4_USER_COUNT, CONFIG_USER_SPACE_LIMIT, CONFIG_STACK_SIZE, CONFIG_HEAP_SIZE, CONFIG_MIN_STACK_SIZE
- [x] Subsystem sizing migrated: MAX_FDS, MAX_MOUNTS, MAX_DRIVERS, MAX_DAEMONS, MAX_PROGRAMS, IPC_*, MAX_SIGNAL_HANDLERS, VFS_MAX_PATH, TASK_NAME_LEN
- [x] MemPool config: CONFIG_MEMPOOL_NUM_POOLS, CONFIG_MEMPOOL_BLOCK_SIZES, CONFIG_MEMPOOL_BLOCK_COUNTS
- [x] INCLUDE_ syscall gating defines for all 35 syscalls
- [x] Architecture feature detection flags: FPU, RDRAND, MPU, HPET, APIC, GIC, PLIC, SBI
- [x] Hook configuration points: IDLE, TICK, STACK_OVERFLOW, OOM, INIT (weak symbols)
- [x] CONFIG_ASSERT macro (overridable, defaults to panic)
- [x] CONFIG_VERSION macro ("0.2.21")
- [x] Duplicate constants consolidated: all PAGE_SIZE (3×) and STACK_SIZE (2×) → CONFIG_*
- [x] Makefile: check-config (toolchain validation script) and config-summary targets
- [x] tools/check-config.py: validates ranges, power-of-2, page alignment, dependency constraints
- [x] All constants migrated from 20+ source files to jarvis_config.h
- [x] 680/680 tests pass after all migrations
- [x] Release tag: v0.2.21

### 0.2.23 — riscv64 Port (RV64)

Follows the same pattern established by v0.2.22, targeting RISC-V 64-bit (RV64) in QEMU virt.

- [x] **A. Boot Entry (`arch/riscv64/boot.S`)**
  - [x] M-mode→S-mode transition via SBI, or start in S-mode (QEMU virt `-bios default`)
  - [x] SATP init with Sv39 page tables (3-level, 4KB pages, 512 GiB virtual address space)
  - [x] Trap vector setup (stvec), S-mode CSRs (sie, sip, sstatus)
  - [x] Identity-map kernel + map higher half, enable MMU, jump to higher half
  - [x] Call `higherhalf_entry(uint64_t magic, uint64_t dtb_ptr)`

- [x] **B. Page Tables (`arch/riscv64/hal/page_table_impl.hpp`)**
  - [x] `ArchPageTable` class with SATP CSR: `current()`, `activate(phys)`, `tlb_flush(va)`, `tlb_flush_all()`
  - [x] Sv39: 3-level page table walk (9-bit each, 4KB pages), `map_page()`, `unmap_page()`
  - [x] Support 2MB and 1GB huge pages (page table entry R/W bits)

- [x] **C. Context Switch (`arch/riscv64/hal/context.hpp`)**
  - [x] `ArchContext` struct: ra, sp, gp, tp, s0–s11, sepc, sstatus
  - [x] Build trap frame for SRET (via `create`/`create_user`/`clone`)
  - [x] Context switch via scheduler save/load globals + trap return (same pattern as x86_64/aarch64)

- [x] **D. Interrupts (PLIC) & Timer**
  - [x] S-mode interrupt delegation: sie.SEIE (external), sie.STIE (timer), sie.SSIE (software)
  - [x] PLIC: init, enable/disable IRQ per context, priority, claim/complete
  - [x] `ArchInterruptController::init()`, `eoi()`, `mask()`, `unmask()`
  - [x] `arch::Timer` via SBI set_timer ecall
  - [x] `Timer::init()`, `ticks()`, `ns()`, `handle_irq()`, `set_frequency()`
  - [x] ECALL handler for syscall entry (U-mode→S-mode)

- [x] **E. UART & Serial**
  - [x] `arch::Serial` — NS16550A UART at `0x10000000` (QEMU virt) via SBI putchar
  - [x] Wire into kernel `Logger`

- [x] **F. RNG**
  - [x] No native RNG in RISC-V ISA. Implement via SBI getrandom extension or ChaCha20 PRNG fallback

- [x] **G. PCI (ECAM)**
  - [x] Same ECAM mechanism as aarch64 — memory-mapped config space

- [x] **H. Remaining HAL surface**
  - [x] `arch::RTC` — via mtime/mtimecmp
  - [x] `arch::cpuid()` — read misa, mvendorid, marchid CSRs
  - [x] `arch::IrqGuard` — RAII via sstatus.SIE (generic in hal/irq_guard.hpp)
  - [x] Keyboard stub (virtio-input later)

- [x] **I. Integration & Tests**
  - [x] `make run ARCH=riscv64` — boots to UART output in QEMU
  - [x] Validate Renode platform `tools/renode/jarvis-riscv64.repl`
  - [x] Initial test class passes on riscv64
  - [x] x86_64 and aarch64 remain fully functional

### 0.2.24 — Cross-Architecture Hardening

- [x] **Architecture test suites** — aarch64 (17 tests, class `arm64`) and riscv64 (18 tests, class `risc64`) covering page tables, context switch, interrupts, timer, FPU, PCI, RTC, boot CSRs
- [x] **Cross-arch atomics** — `kernel::atomic<T>` wrapper, 12 `__sync_synchronize()` replaced, `__atomic_*` wrapped in spinlock/spsc/ring_buffer/dmesg
- [x] **Boot flow unification** — libfdt subset ported, `BootInfo` struct, `higherhalf_entry()` unified, FDT memory parsing for aarch64/riscv64
- [x] **UART driver abstraction** — `arch/hal/serial.hpp` pure interface, x86_64 impl in own `.cpp`, `Logger` uses uniform API
- [x] **Renode CI** — `make renode-test ARCH=aarch64` and `ARCH=riscv64` as CI gate
- [x] **Virtio transport unification** — unified PCI HAL (CF8/CFC + ECAM), shared `virtio_pci.cpp`, ECAM for aarch64/riscv64
- [x] **Memory model tests** — 12 atomic tests (load/store/exchange/CAS/SB/MP/pingpong, acquire/release ordering)
- [x] **Cross-arch test suite** — `test_cross_arch.cpp` with 16 shared tests (page table, context, timer, interrupts, IPC, VFS)

### 0.2.22 — aarch64 Port (ARM Cortex-A)

Builds on `jarvis_config.h` (v0.2.21) to bring Jarvis up on ARM Cortex-A in QEMU virt. Every architecture-dependent surface (page tables, interrupts, context switch, timer, boot) gets an `arch/aarch64/` implementation.

- [x] **A. HAL Interface Refactoring (structural)**
  - [x] Move `arch/x86_64/timer.cpp`, `gdt.cpp`, `idt.cpp` → `arch/x86_64/hal/` with interface headers matching `arch/hal/` API
  - [x] `arch/hal/context.hpp` — make `ArchContext` arch-selected (x86_64 vs aarch64 vs riscv64)
  - [x] `arch/hal/io.hpp` — add `#elif CONFIG_ARCH_AARCH64` branch mapping port I/O to MMIO (`arch/aarch64/hal/io_impl.hpp`)
  - [x] `arch/hal/page_table.hpp` — dispatches to arch-specific `page_table_impl.hpp` per arch
  - [x] Build system: arch-specific `OBJ` lists in `mk/rules.mk` via `arch/$(ARCH)/` source discovery
  - [x] Validate `linker_aarch64.ld` — links successfully, sections/symbols verified via `make build/kernel-debug.elf ARCH=aarch64`

- [x] **B. Boot Entry (`arch/aarch64/boot.S`)**
  - [x] EL2→EL1 transition (QEMU virt starts at EL2), or stay at EL1 if configured
  - [x] Exception level drop, VBAR_EL1 vector table install
  - [x] MMU init: TCR_EL1 (4KB granule, 4-level), MAIR_EL1 (normal/device memory), TTBR0_EL1/TTBR1_EL1 with 4-level page tables
  - [x] Identity-map kernel low region + map higher half using boot page tables
  - [x] Enable MMU (SCTLR_EL1.M), jump to higher half
  - [x] Call `higherhalf_entry(uint64_t magic, uint64_t dtb_ptr)` — device tree pointer instead of multiboot

- [x] **C. Page Tables (`arch/aarch64/hal/page_table_impl.hpp`)**
  - [x] `ArchPageTable` class: `current()`, `activate(phys)`, `tlb_flush(va)`, `tlb_flush_all()`
  - [x] 4-level page table walk (L0–L3, 9-bit each, 4KB granule), `map_page()`, `unmap_page()`, `get_physical()`
  - [x] Support 2MB block mappings at L2 (huge pages)

- [x] **D. Context Switch (`arch/aarch64/hal/context.hpp`)**
  - [x] `ArchContext` struct: x0–x29, x30/LR, SP_EL1, ELR_EL1, SPSR_EL1
  - [x] `ArchContextManager::init_stack()` — build initial pt_regs frame for ERET to EL0
  - [x] `ArchContextManager::switch_to(from, to, rsp)` — save/restore callee-saved regs, SP_EL1, ELR_EL1
  - [x] `switch_to_task()` assembly trampoline — context-switch via ERET

- [x] **E. Interrupts & Generic Timer**
  - [x] DAIF masking: `irq_enable()`/`irq_disable()` via `MSR DAIFClr/DAIFSet, #2`
  - [x] GICv3: distributor init, CPU interface init, SPI/PPI routing, eoi
  - [x] `ArchInterruptController::init()`, `eoi()`, `mask()`, `unmask()`
  - [x] VBAR_EL1 exception vector table (~32 entries): sync, IRQ, FIQ, SError × 4 exception levels
  - [x] SVC #0 handler for syscall entry (EL0→EL1)
  - [x] `arch::Timer`: init via CNTP_TVAL_EL0, ticks via ticks_ counter, ns via CNTPCT_EL0
  - [x] `Timer::set_frequency()` — program CNTP_TVAL_EL0 period, no calibration needed (CNTFRQ_EL0 is fixed)

- [x] **F. UART & Serial**
  - [x] `arch::Serial` — PL011 UART at `0x9000000` (QEMU virt): init (8N1), putc, getc
  - [x] Wire into kernel `Logger::init()` and `debug_write()` via `uart_putc()`

- [x] **G. PCI (ECAM)**
  - [x] Replace CF8/CFC port I/O with ECAM memory-mapped config space at QEMU virt ECAM base — ECAM driver implemented in `pci_impl.hpp`, wired via `pci.hpp` `CONFIG_ARCH_AARCH64` branch
  - [x] PCI bus scan, BAR parsing, MSI/MSI-X capability detection — generic `pci.cpp` uses arch-specific accessors (ECAM on aarch64, CF8/CFC on x86_64)

- [x] **H. Remaining HAL surface**
  - [x] `arch::RTC` — ARM Generic Timer based (CNTPCT_EL0 / CNTFRQ_EL0)
  - [x] `arch::cpuid()` — read ID_AA64*_EL1 system registers for FPU/SIMD feature detection (`arch/aarch64/hal/cpuid_impl.hpp`)
  - [x] `arch::IrqGuard` — RAII via DAIF masking
  - [x] `arch::rdrand64()` — via `arch/aarch64/hal/rand_impl.hpp` (RNDRRS_EL0 or ChaCha20 fallback)
  - [x] `arch::Keyboard` — stub (no PS/2 on ARM virt); future: virtio-input

- [x] **I. Integration & Tests**
  - [x] `make run ARCH=aarch64` — boots to kernel UART output (debug builds + safe-class tests)
  - [x] Validate Renode platform `tools/renode/jarvis-aarch64.repl`
  - [x] Port kernel selftest framework to aarch64 (test registration + serial output) — safe class runs
  - [ ] `make test-all-debug ARCH=aarch64` — all test classes pass (safe class OK, full suite has failures)
  - [x] x86_64 must remain fully functional through every change

### 0.2.25 — Test Safety & RAII Hardening

- [x] **Eliminate dangling pointer accesses in tests** — convert remaining raw `delete ptr; ptr->member` patterns to `ScopeGuard` or `UniquePtr<T>` with custom deleter
  - [x] `test_task_lifecycle.cpp` — `TaskPtr`/`SimpleTaskPtr` for 4 single-TCB tests, `ScopeGuard` for 4 multi-TCB tests
  - [x] `test_waitpid.cpp` — evaluated, 3-TCB pattern with asymmetric cleanup; `ScopeGuard` not cleaner than existing manual cleanup
  - [x] `test_buffer_pool.cpp` — `SimpleTaskPtr` for 17 single-TCB tests, `ScopeGuard` for 5 dual-TCB tests
  - [x] `test_spinlock.cpp`, `test_preemption_under_syscall.cpp` — removed `guard.dismiss()` + manual redo anti-pattern in all 6 sites
  - [x] Add `UniquePtr<T, Deleter>` usage guide to code style docs
