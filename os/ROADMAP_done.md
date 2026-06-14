# Jarvis RTOS — Completed Milestones

## Phase 1: Code Refactoring & Microkernel Foundation (0.2.7–0.2.11)

### 0.2.7 — Error Assertion Retrofit ✓
- [x] ASSERT(err) / ENSURE(cond) macros + per-module error_string<E> specialisation
- [x] Module error headers: task, PMM, VMM (X-macro pattern)
- [x] Retrofitted: task, scheduler, PMM, VMM, MemPool, IPC — all ASSERT/ENSURE call sites
- [x] Retrofit sync (mutex, semaphore, queue, eventgroup, notify)
- [x] Retrofit syscall handlers (process, fs, ipc, sync, misc)
- [x] Retrofit VFS (resolve, mount, FdTable, initrd_fs, devfs, procfs, pipe)
- [x] Retrofit ELF loader, driver registry, kernel.cpp, bootparams.cpp

### 0.2.8 — Code Refactoring & Streamlining ✓
- [x] O(1) syscall dispatch table, MSR-based syscall/sysret entry
- [x] Type-safe PhysicalAddress/VirtualAddress wrappers, no raw reinterpret_cast
- [x] Unified Arch::Serial + Logger, inline-asm purged from non-arch/ code
- [x] Linker GC, function/data sections, const correctness, reference params

### 0.2.9 — Microkernel Foundation & Task Lifecycle ✓
- [x] Privilege audit, IPC latency benchmark, unprivileged userspace shell
- [x] Task lifecycle audit: reap_orphans fix, elf::load init_task_common, blocked-sender cleanup, page_table_shared_ use-after-free
- [x] Userspace idle task (Ring 3 pause loop)
- [x] Userspace server infrastructure: VFS daemon (vfsd), I/O daemon (iocd)
- [x] Capability-based MMIO access control
- [x] Wait-for-graph deadlock detection engine (WFG)
- [x] Deadlock recovery: terminate task, release locks, unblock waiters
- [x] Health status syscall and /proc/health metrics
- [x] Full audit: bugs #009-#019 closed
- [x] 1233 kernel self-tests PASS

### 0.2.10 — Zero-Copy Buffer Pool ✓
- [x] Zero-copy buffer pool (1024 buffers, 4 KiB each, generation-counter handles, O(1) IPC transfer)
- [x] SYS_BUF_ALLOC/FREE/MAP/UNMAP syscalls (37-40)
- [x] IPC::send integration (buf_handle field in Message)
- [x] cleanup() + exec() buffer teardown, fork buffer isolation
- [x] 11 buffer-pool kernel tests, 2329/2331 PASS
- [x] Userspace VFS server (vfsd) hardening + crash recovery
- [x] I/O daemon (iocd) hardening + crash recovery

### 0.2.11 — Coding Style Refactoring ✓
- [x] Const correctness retrofit — `const` on all kernel variables, params, member functions
- [x] References over pointers — migrate non-nullable `T*` params to `T&`
- [x] All variables initialized — fix every uninitialized local declaration
- [x] Constructor init-list migration — member assignments in body → member initializer list
- [x] Meaningful sentinel enums — replace raw `-1` checks with named constants
- [x] Descriptive names — rename blocklisted single-char vars (`t`, `v`, `val`, `tmp`, `ptr`, `p`)
- [x] Remove `const_cast` — use `mutable` or redesign to avoid const modification
- [x] Bounded loops — replace unbounded `while (true)`/`for (;;)` with max-iteration guards
- [x] Dynamic allocation audit — replace `new`/`delete` on kernel paths with fixed pools
- [x] Documentation Doxygen compliance — `@brief`, `@param`, `@return` on all public APIs
- [x] Validation — zero errors from `make check-style` (exit 0)
- [x] Test isolation: full snapshot/restore (PMM, MemPool, Scheduler, DaemonManager, BufferPool, ResourceTracker)
- [x] ResourceTracker: leak detection with backtrace, test name in output
- [x] PMM/MemPool leak fixes (PML4 clone, huge-page split, VMM page tables, ELF failure paths)
- [x] IRQ-safe snapshot with RFLAGS save/restore
- [x] Linker script: split boot segment into separate R+X and R+W segments
- [x] 2661 kernel self-tests PASS, 0 failures
