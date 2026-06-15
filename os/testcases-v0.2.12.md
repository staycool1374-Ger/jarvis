# Test Cases — v0.2.12 (Phase 3: System Services)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### tmpfs (Temporary Filesystem) — test_tmpfs.cpp
- Basic mount/unmount lifecycle
- File create, read, write, close round-trip
- Directory creation and listing
- Hard link quota enforcement
- User-specific quota limits
- OOM protection: allocation rejected at quota boundary
- Unlinking frees pages
- Concurrent read/write from multiple tasks

### Init System (PID 1) — test_init.cpp
- `/sbin/init` boots as PID 1 in userspace
- Service lifecycle: spawn, monitor, restart on crash
- `/etc/rc` script parsing and sequential execution
- Orphaned children reparented to init
- SIGCHLD handling from terminated children

### fstab — test_fstab.cpp
- fstab format parsing (device, mountpoint, fstype, options)
- Auto-mount all entries at boot
- Invalid fstab entries are skipped gracefully
- Missing device in fstab reports error
- Duplicate mountpoint detection

### Resource Limits & Heap — test_rlimit.cpp
- `SYS_GETRLIMIT` / `SYS_SETRLIMIT` on max_fds, max_stack, max_memory
- Exceeding `max_fds` returns EMFILE
- Exceeding `max_stack` triggers segfault
- `SYS_BRK` expands heap (positive increment)
- `SYS_BRK` contracts heap (negative increment)
- `SYS_BRK` with invalid address returns -1
- brk/sbrk wrapper in libc matches kernel behavior

### Core Text Utilities — test_textutils.cpp
- `/bin/less` paginates stdin, handles up/down arrows
- `/bin/ed` opens file, accepts line-oriented commands
- Signal handling (SIGINT, SIGTERM) during pager session

---

### Preemption — new test_preemption.cpp

- `preemption_needs_switch_higher_priority`: `needs_switch()` returns `true` when a higher-priority task is READY and current is lower priority
- `preemption_needs_switch_equal_priority`: `needs_switch()` returns `false` when tasks share same priority (round-robin is handled by tick, not by switch check)
- `preemption_needs_switch_blocked_higher`: `needs_switch()` returns `false` when higher-priority task is BLOCKED (not runnable)
- `preemption_disabled_blocks_switch`: `set_preemptible(false)` causes `needs_switch()` to return `false` even with a higher-priority READY task present
- `preemption_quantum_exhaustion`: task with `remaining_ticks=0` causes `needs_switch()` to return `true`; after reload, `remaining_ticks` equals `period_ticks`
- `preemption_interrupt_enable_disable_cycle`: toggling preemption on/off repeatedly without crashing; state always consistent after each toggle
- `preemption_task_switch_does_not_switch_to_self`: `next_task()` never returns the same task when another READY task exists at the same priority

### Locking — test_sync.cpp (additions)

- `mutex_try_lock_success`: `try_lock()` returns `true` on an unlocked mutex; `is_locked()` and `owner()` reflect the new lock state
- `mutex_try_lock_failure`: `try_lock()` returns `false` when mutex is held by a different task; state unchanged
- `mutex_try_lock_recursive_same_owner`: `try_lock()` returns `true` when the same owner calls it recursively (recursive mutex behavior)
- `mutex_priority_inheritance_indirect`: low-priority task holds a mutex; high-priority task attempts to lock it; the low-priority task's `priority` field is temporarily boosted to the high-priority level; after the low-priority task unlocks, its priority returns to `base_priority`
- `mutex_priority_chain`: three-task chain A→B→C (A holds mutex M1, B waits on M1, C waits on M2 held by B, which is blocked); verify that priority propagates correctly through the chain by reading each task's `priority` field
- `mutex_waiter_priority_order`: multiple tasks blocked on a mutex at different priorities; when the holder unlocks, the highest-priority waiter acquires the lock next (not FIFO)
- `mutex_double_lock_same_owner`: mutex locked twice by the same owner does not deadlock (recursive mutex); correct unlock count required for full release
- `mutex_lock_acquire_release_cycle`: 100 rapid lock/unlock cycles on the same mutex; no corruption, no stray waiters, owner correctly NULL after final unlock
- `semaphore_wait_priority_order`: multiple tasks blocked on `Semaphore::wait()` at different priorities; `post()` wakes the highest-priority task first
- `semaphore_multi_waiter_partial_wake`: `post(3)` with 5 blocked waiters wakes exactly 3, leaves 2 blocked; the woken tasks have `state == READY`, the blocked ones have `state == BLOCKED`
- `semaphore_initial_count_zero`: `Semaphore` initialized with `count=0`; `wait()` blocks immediately; `post()` makes it pass
- `queue_send_receive_priority_ordering`: blocked senders/receivers in `Queue` are woken in priority order, not FIFO order
- `queue_send_to_full_blocks_and_wakes`: queue filled to capacity; `send()` blocks the caller; a consumer `receive()` wakes the blocked sender; message is correctly delivered

(End of file - total 79 lines)