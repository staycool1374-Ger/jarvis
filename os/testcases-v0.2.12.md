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

### Scheduler — test_scheduler.cpp (additions)

- `scheduler_alloc_id_sequential`: `Scheduler::alloc_id()` returns monotonically increasing IDs; wraps correctly from `UINT64_MAX`
- `scheduler_task_at_bounds`: `task_at(0)` returns idle task; `task_at(count-1)` returns last real task; `task_at(count)` returns `nullptr`
- `scheduler_find_task_nonexistent`: `Scheduler::find_task(999999)` returns `nullptr`
- `scheduler_set_preemptible_toggle`: `is_preemptible()` reflects `set_preemptible(true)` and `set_preemptible(false)`
- `scheduler_current_task_after_switch`: after `next_task()` selects a different task, `set_current()` + `current_task()` returns the new one
- `scheduler_add_duplicate_id`: adding a task with a duplicate ID is handled gracefully (no crash, no corruption)

### Preemption — new test_preemption.cpp

- `preemption_needs_switch_higher_priority`: `needs_switch()` returns `true` when a higher-priority task is READY and current is lower priority
- `preemption_needs_switch_equal_priority`: `needs_switch()` returns `false` when tasks share same priority (round-robin is handled by tick, not by switch check)
- `preemption_needs_switch_blocked_higher`: `needs_switch()` returns `false` when higher-priority task is BLOCKED (not runnable)
- `preemption_disabled_blocks_switch`: `set_preemptible(false)` causes `needs_switch()` to return `false` even with a higher-priority READY task present
- `preemption_quantum_exhaustion`: task with `remaining_ticks=0` causes `needs_switch()` to return `true`; after reload, `remaining_ticks` equals `period_ticks`
- `preemption_interrupt_enable_disable_cycle`: toggling preemption on/off repeatedly without crashing; state always consistent after each toggle
- `preemption_task_switch_does_not_switch_to_self`: `next_task()` never returns the same task when another READY task exists at the same priority

### Timing / Tick-Driven Scheduling — new test_timing.cpp

- `timer_tick_accounting`: calling `Scheduler::on_tick()` increments `current_task().executed_ticks` by exactly 1
- `timer_period_reload`: when `remaining_ticks` reaches 0, calling `on_tick()` reloads it to `period_ticks` and resets `executed_ticks`
- `timer_alarm_delivery`: set `alarm_ticks=3`; call `on_tick()` 3 times; after 3rd tick `alarm_ticks == 0` and SIGALRM is pending in signal mask
- `timer_alarm_not_expired`: set `alarm_ticks=10`; call `on_tick()` 5 times; `alarm_ticks == 5` and no SIGALRM is pending
- `timer_rate_monotonic_schedule_indirect`: `on_tick()` triggers `rate_monotonic_schedule()` which causes `needs_switch()` to return `true` when a higher-priority task is overdue
- `timer_reap_orphans_periodic`: after a child task is TERMINATED with a dead parent, `on_tick()` eventually reaps it (observable via `task_count()` decrease)
- `timer_no_side_effects_on_idle`: calling `on_tick()` when only the idle task is eligible does not crash or corrupt scheduler state
- `timer_daemon_restart_not_triggered_on_active`: daemon tasks in RUNNING state are not restarted by `on_tick()`

### IPC Message Queues — test_ipc.cpp / test_ipc_extended.cpp / test_ipc_blocking.cpp (additions)

- `ipc_notify_wait_blocking`: `Notify::wait()` blocks the caller when no notification is pending; `notify(42)` wakes it and `wait()` returns 42
- `ipc_notify_wait_already_pending`: `notify(99)` followed immediately by `wait()` returns 99 without blocking
- `ipc_eventgroup_wait_bits_blocking`: `EventGroup::wait_bits(0x03)` blocks when bits not set; `set_bits(0x03)` wakes it; returned value matches the bits that satisfied the wait
- `ipc_eventgroup_wait_bits_clear_on_exit`: `wait_bits(0x05, true)` blocks; `set_bits(0x05)` wakes it; after wake `get_bits()` returns 0 (bits cleared)
- `ipc_eventgroup_wait_bits_partial_match`: `wait_bits(0x03)` is NOT woken by `set_bits(0x01)` alone — must match ALL requested bits
- `ipc_eventgroup_multiple_waiters`: two tasks wait on different bit patterns; `set_bits(0x01)` wakes only the first; `set_bits(0x04)` wakes only the second
- `ipc_queue_accessor`: `IPC::queue(task_id)` returns a valid `MessageQueue&` for an existing task; calling with a nonexistent task ID hits `ENSURE` (or returns null, depending on implementation)
- `ipc_blocked_senders_priority_order`: multiple senders blocked on a full queue at different priorities; when a slot opens via `recv()`, the highest-priority sender is woken first (not FIFO)
- `ipc_send_to_blocked_self`: task blocks on `recv()`, then another task sends to it; the receiving task wakes and reads the correct message
- `ipc_send_sync_multiple_roundtrips`: `send_sync()` handles 10 rapid request/reply round-trips on the same queue without corruption or message mix-up
- `ipc_queue_empty_after_drain`: fill queue to max, drain all messages; `is_empty()` returns `true`, `highest_priority()` returns `IPC_PRIORITY_LEVELS`
- `ipc_send_large_payload_boundary`: send with `data_size == IPC_MAX_MSG_SIZE` (64 bytes) is accepted; `data_size == IPC_MAX_MSG_SIZE + 1` is rejected
- `ipc_recv_without_send`: calling `recv()` on an empty queue with `IPC_NONBLOCK` returns immediately (no block)

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

### Locking Edge Cases — new test_locking_stress.cpp

- `mutex_stress_high_contention`: N tasks (N=8) repeatedly lock/unlock a shared mutex; every task makes progress; no task is starved indefinitely; no deadlock after 10,000 cycles
- `semaphore_producer_consumer`: one producer task posts to a semaphore; multiple consumer tasks wait; each message is consumed exactly once; no lost wakeups (matches the classic producer-consumer correctness test)
- `queue_multi_producer_multi_consumer`: 4 producers and 4 consumers sharing a single `Queue`; no message lost, no duplicate delivery, no crash after 5,000 messages
- `priority_inversion_under_contention`: low-priority task holds mutex; medium-priority task is CPU-bound (preempting low); high-priority task contends for mutex; verify that the medium task does NOT prevent low from releasing the mutex (priority inheritance prevents inversion)
- `lock_deadlock_detection`: create a cycle (A→B→A); verify that the deadlock detector detects it and triggers recovery (test_deadlock_detect.cpp counterpart)
