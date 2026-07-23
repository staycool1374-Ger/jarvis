# Test Cases — v0.3.3 (Phase 4: Inheritance & Ceiling)

## Branch: main

*Outline — test details to be expanded when implementation begins.*

### Priority Inheritance Protocol — Mutex — test_pip_mutex.cpp
- 3-task chain (low holds lock, medium preempts, high waits for lock) — verify low inherits high's priority
- Priority inheritance released when high times out or lock released
- Nested mutex: low holds A, waits for B held by medium; medium inherits max(waiter priorities)
- Boosting does not exceed `CONFIG_MAX_PRIORITY`
- Owner change on inheritance is visible to all cores (SMP)
- `CONFIG_MUTEX_PIP=0` build: inheritance disabled, standard priority inversion observable
- `CONFIG_MUTEX_PRIORITY_CEILING` compile-time validation of ceiling values
- No spurious wake or missed unlock after inheritance chain

### Priority Inheritance — Semaphore — test_pip_semaphore.cpp
- Binary semaphore: owner tracking + PIP — low-prio holder boosted when high-prio waiter blocks
- Counting semaphore: last-waiter priority determines boost amount
- Semaphore with multiple waiters: boost to highest waiting priority
- Uncontested lock/unlock has zero PIP overhead (fast path)
- `CONFIG_SEMAPHORE_PIP=0` build: standard semaphore behaviour retained

### Priority Inheritance — Message Queue — test_pip_queue.cpp
- Queue::send_waiters / recv_waiters: high-prio sender blocked on full queue boosts receiver
- High-prio receiver blocked on empty queue boosts sender
- Priority donation returned to original on queue operation completion
- Nested boost: task inherits multiple priorities across different queues
- `CONFIG_QUEUE_PIP=0` build: queue without inheritance

### Priority Ceiling Protocol — test_pcp.cpp
- Mutex ceiling = static max priority of all tasks that may lock the mutex
- On lock attempt: block if caller priority ≤ system ceiling (max of all held ceilings)
- PCP prevents deadlock: 2-task, 2-mutex circular-wait scenario resolved by ceiling blocking
- PCP prevents chained blocking: high-prio task not blocked by multiple lower-prio holders
- Ceiling violation detection: task tries to lock mutex with ceiling lower than current system ceiling → blocked
- `CONFIG_PRIORITY_CEILING_PROTOCOL=1` + `CONFIG_MUTEX_PIP=1`: combined PIP+PCP mode
- `CONFIG_PRIORITY_CEILING_PROTOCOL=1` alone: PCP without PIP (ceiling-based blocking only)

### Scheduler Preemption Points — test_preemption_points.cpp
- Every `cli`/`sti` pair audited: preemption check at each `sti`
- `Scheduler::reschedule()` called from: syscall return, ISR exit, `on_tick`, explicit yield
- Preemption disabled region bounded to < CONFIG_PREEMPTION_LATENCY_MAX_CYCLES
- `CONFIG_PREEMPTION_LATENCY_MAX_CYCLES` measurement — `rdtsc` before/after preempt-off region
- Assertion fires if measured latency exceeds configured max
- Nested preempt-disable: outermost `sti` triggers reschedule only once
- No preemption in IRQ handler tail-chaining (nested ISR non-preemptible)
