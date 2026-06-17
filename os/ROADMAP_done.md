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
