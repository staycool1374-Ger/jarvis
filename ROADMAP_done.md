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

## Phase 3: System Services & Hardware (v0.12.14–v0.2.22)

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
