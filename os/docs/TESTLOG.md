# Jarvis RTOS Test Log

## Version 0.2.9

**Date:** 2026-06-11

### Test Results: 1233/1233 PASS (debug), 174/174 PASS (release), all integration tests PASS

| Category | Tests | Status |
|----------|-------|--------|
| string   | 7     | ✅ All PASS |
| utils    | 3     | ✅ All PASS |
| ErrorOr  | 2     | ✅ All PASS |
| PMM      | 5     | ✅ All PASS |
| VMM      | 4     | ✅ All PASS |
| MemPool  | 4     | ✅ All PASS |
| IPC      | 14    | ✅ All PASS |
| Scheduler| 4     | ✅ All PASS |
| Task     | 5     | ✅ All PASS |
| Driver   | 2     | ✅ All PASS |
| VFS      | 8     | ✅ All PASS |
| Version  | 3     | ✅ All PASS |
| ELF      | 9     | ✅ All PASS |
| CheckedPtr | 3   | ✅ All PASS |
| Signal   | 8     | ✅ All PASS |
| Process  | 4     | ✅ All PASS |
| RTC      | 2     | ✅ All PASS |
| Pipe     | 7     | ✅ All PASS |
| Devfs/Procfs/Initrdfs | 2+3+3 | ✅ All PASS |
| Sync     | 4     | ✅ All PASS |
| Capability | 16  | ✅ All PASS |
| Task Lifecycle | 8 | ✅ All PASS |
| Idle Task | 8   | ✅ All PASS |
| VFS Daemon | 12 | ✅ All PASS |
| I/O Daemon | 8  | ✅ All PASS |
| WFG      | 4     | ✅ All PASS |
| Deadlock Detection | 5 | ✅ All PASS |
| Deadlock Recovery | 5 | ✅ All PASS |
| Health   | 4     | ✅ All PASS |
| Timer    | 5     | ✅ All PASS |
| Serial   | 3     | ✅ All PASS |
| Keyboard | 5    | ✅ All PASS |
| GDT      | 5    | ✅ All PASS |
| IDT      | 6    | ✅ All PASS |
| Bootparams | 4  | ✅ All PASS |
| Multiboot | 5  | ✅ All PASS |
| Address  | 5    | ✅ All PASS |
| Stress   | 6    | ✅ All PASS |
| PIC      | 3    | ✅ All PASS |
| Integration | 5 | ✅ All PASS |
| PML4 Clone | 6 | ✅ All PASS |
| Waitpid  | 3    | ✅ All PASS |
| IPC Benchmark | 7 | ✅ All PASS |
| GCOV     | 3    | ✅ All PASS |
| Debug    | 3    | ✅ All PASS |
| Framebuffer | 5 | ✅ All PASS |
| PML4 Dump | 1  | ✅ All PASS |

### Bugfixes in v0.2.7-v0.2.9
- #009: Second fork() zombie accumulation — orphan child on exit to allow reap
- #010: reap_orphans() missing reparent + deferred reap condition
- #011: Task lifecycle — elf::load missing init_task_common, blocked-sender cleanup, page_table_shared_ use-after-free
- #012: Release build test_fork — HHDM_OFFSET, CR3 switch, RAX return value
- #013: sys_exec() — missing HHDM offset in file buffer pointer
- #014: IPC::send() — doesn't wake BLOCKED destination task
- #015: clone() — private PDPT subtree leak on child cleanup
- #016: cleanup() — msg_queue leak with blocked senders
- #017: free_user_pages() — misleading invlpg TLB flush
- #018: reap_orphans() — WONTFIX, original shift logic retained
- #019: remove_child() — num_children underflow guard
- idle_task: Ring 3 pause loop, restarts on crash, restartable
- Mandelbrot CRC hash test for fork correctness
- Wait-for-graph deadlock detection and recovery engine
- VFS daemon (vfsd) and I/O daemon (iocd) userspace servers
- Capability-based MMIO access control for driver isolation

## Version 0.2.5

**Date:** 2026-06-09

### Test Results: 410/410 PASS

### Bugfixes in v0.2.5
- Priority IPC Redesign: per-TCB MessageQueue with priority bitmap
- O(1) task-ID→TCB lookup via hash table
- Priority inheritance for IPC senders
- IPC_NONBLOCK flag for non-blocking send/receive
- Notify and EventGroup exposed to userspace

## Version 0.2.4

**Date:** 2026-06-06

### Test Results: 39/39 PASS (267 assertions, 0 failures)

| Category | Tests | Status |
|----------|-------|--------|
| string   | 7     | ✅ All PASS |
| utils    | 3     | ✅ All PASS |
| ErrorOr  | 2     | ✅ All PASS |
| PMM      | 4     | ✅ All PASS |
| MemPool  | 4     | ✅ All PASS |
| IPC      | 4     | ✅ All PASS |
| Driver   | 2     | ✅ All PASS |
| Scheduler| 2     | ✅ All PASS |
| VFS      | 8     | ✅ All PASS |
| Version  | 3     | ✅ All PASS |

### Bugfixes in v0.2.4
- Fixed RFLAGS reserved bit 1 in kernel task creation (0x200 → 0x202)
- Extended TaskContext struct to match 22-item stack frame layout
- Added user-task guard to SYS_EXEC syscall
- KEEP() guard on `.multiboot_header` section for `--gc-sections` compat
- Added `strncpy` to kernel string library
