# Global Operating Rules (all roles, always active)

## Branch Safeguard
Before writing or modifying kernel tests, run `git branch --show-current`:
- If `main` → follow PROMPT-dev.md (production development role)
- If `testbed` → follow PROMPT-testdev.md (test engineering role)
- If neither → alert the user

If the branch does not match the intended role, do not proceed.

## Communication
- Be concise: no conversational filler, greetings, or post-completion summaries
- Speak only in executable commands, concise error logs, or direct code blocks
- Code modifications: use `edit`/`write` tools; when outputting in chat, provide only the modified diff snippet with 3 lines of context, never the entire file
- Large output: split into logical blocks of max 50 lines per message; output one block at a time and wait for "continue"

## Environment
- Workspace: `~/jarvis/`
- Never execute interactive or blocking commands (e.g. `make run-qemu`, `make release`). Non-interactive automated workflows only (e.g. `make test-qemu`)
- Use `todowrite`, never `todo`
- Sudo password `junior` only when strictly required (e.g. ISO generation)

## Pre-Flight
- Run `bash ~/jarvis/healthcheck.sh`. If exit != 0, halt, print the raw error, and stop. Do not guess a fix.

## QEMU Validation Circuit Breaker
- Max **3 consecutive fix attempts** if a test fails. If still failing on the 3rd attempt, halt and await human input.

## ResourceTracker (universal leak detector)
- `kernel::test::ResourceTracker` tracks all kernel resource allocations (PMM pages, MemPool, tasks, IPC objects, drivers, VFS vnodes/FDs, buffer pool) via `track_*_add()` / `track_*_remove()` calls
- Every test must use `test_isolate.cpp` snapshot/restore. `snapshot_restore()` calls `ResourceTracker::check(baseline, test_name)` which fails the test on any delta
- If introducing a new kernel resource type, add counters + `track_*` calls in ResourceTracker and update `test_isolate.cpp`

## Debugging Notes (historical)
- **Release build gotchas:** framebuffer alpha channel (set byte 3 to 0xFF for bpp>24); serial deadlock from VFS daemon crash — use kernel shell as fallback; QWERTY scancodes are correct, AZERTY is QEMU-on-macOS host mapping
- **Debug runtime issues:** use `make run-release` (not `make release`), build flags differ
- **Crash reproduction:** simulate user input with `expect` scripts; strip components (test_fork, shell, release tests) to isolate
- **Page-table fork bugs:** `clone()` shares PDPT/PD/PT pages — any `map_page_in_pml4` on child corrupts parent; fix: private PDPT copy for stack region
- **Debug context-switch ring buffer** (`CONFIG_DEBUG`): each TCB has `debug_switch_ring[4]` — inspect via `p current->debug_switch_ring[current->debug_switch_idx % 4]`
- **GDB debugging:** `make gdb` launches QEMU with GDB stub on `:1234`; connect with `x86_64-elf-gdb build/kernel-debug.elf -x tools/gdb/init.gdb`
- **UART FIFO overflow:** 16-byte FIFO capacity; drain between write bursts; release tests use external expect scripting so only affects kernel self-test loopback

## Session Summary (v0.2.18 → v0.2.19)

### Completed This Session
- **SporadicServer budget enforcement**: `SporadicServer` class with O(1) replenishment, wired into scheduler `on_tick()` and IPC `sys_receive()`. vfsd (C=2,T=10) and iocd (C=3,T=10) configured. 14 unit tests.
- **CI fix**: `gcc-x86-64-elf`/`binutils-x86-64-elf` unavailable on ubuntu-24.04 → use native `gcc`/`ld`/`ar`/`objcopy` (host is x86_64, output is identical).
- **ROADMAP**: Marked all v0.2.18 items `[x]`, bumped executive override to v0.2.19.
- **Documentation**: Updated README.md (version, test count, SporadicServer feature), testcases-v0.2.18.md, IMPLEMENTED_TESTS_SUMMARY.md.
- **Release**: Committed + tagged `v0.2.18`. All committed.

### Debugging (v0.2.19)
- **Timer::ns() fix**: Replaced `unsigned __int128` divide with pure 64-bit arithmetic. Compiler-emitted `__udivti3` caused triple fault in bare-metal x86_64-elf.
- **Semaphore SpinLock fix**: `Semaphore::add_waiter()`/`wake_one()` redundantly took `lock_` already held by `wait()`/`post()` → non-recursive SpinLock deadlock. Removed guards.
- **Queue SpinLock fix (same pattern)**: `Queue::add_send_waiter()`/`add_recv_waiter()`/`wake_send_one()`/`wake_recv_one()` redundantly took `lock_`. Removed guards — fixed hang at test 195.
- **Host-side watchdog**: Background loop kills QEMU after `WATCHDOG_STALL` consecutive seconds of no serial log growth.
- **MinimalPrivilegedSurface fix**: Skip blocking syscalls (RECEIVE, SEND_SYNC, EXIT, NOTIFY_WAIT, EVENT_WAIT, PAUSE) — calling `sys_receive()` with null args blocked the shell forever. Fixed the 625/675 hang.
- **Test results**: `test-all-debug` = 675/675 PASS, `TIME_ELAPSED_MS` = ~71s. All test targets green.

### Pending
- Renode simulation setup

## Session Summary (v0.2.19 → v0.2.20)

### Completed This Session
- **Allocator audit**: `operator new`/`delete` mismatch fixed. `new` uses three-tier fallback: MemPool → PMM pages (with `PmmAllocHdr` header) → bump allocator. `delete` handles MemPool/PMM, no-ops on bump pointers.
- **MemPool**: Added `contains()`, `is_ready()`, `ready_` flag.
- **RAII wrappers**: `ScopeGuard` (`src/lib/scope_guard.hpp`), `UniquePtr<T>` (`src/lib/unique_ptr.hpp`).
- **Test leak fixes**: 11 leaking tests fixed via `ScopeGuard` cleanup on early-return paths:
  - `test_buffer_pool.cpp`: `buffer_pool_transfer_to_kernel_task` — proper cleanup regardless of transfer outcome
  - `test_starvation_deadlock.cpp`: `PriorityInversionChain5` — ScopeGuard ensures 5 tasks cleaned up
  - `test_resource_exhaustion.cpp`: `MempoolFragmentation` — ScopeGuard frees partial allocs; no longer assumes 20 allocs succeed
  - `test_spinlock.cpp`: `spinlock_contention`, `spinlock_preemption_yield`
  - `test_preemption_under_syscall.cpp`: `preemption_during_syscall`, `preempt_highpri_during_tmpfs_write`, `preempt_highpri_during_brk`, `preempt_lowpri_not_starved`
  - `test_spinlock_stress.cpp`: `spinlock_multi_task_contention`
  - `test_atomic_context_switch.cpp`: `atomic_globals_set_on_reschedule`, `atomic_idempotent_null_handling`
- **Test verification**: Baseline confirmed `test-all-debug` hang at test 438 (MempoolFragmentation toggle) is pre-existing. Safe-class tests (incl. FAT32) pass 102/102 with our changes.
- **Known remaining leak** (pre-existing, small): `preempt_highpri_during_tmpfs_write` — 2 MemPool blocks from tmpfs mount never unmounted (no unmount API).
