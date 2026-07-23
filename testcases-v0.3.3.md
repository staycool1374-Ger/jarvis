# Test Cases — v0.3.3 (Phase 4: PI & PCP)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Mutex Priority Inheritance — test_mutex_pip.cpp
- Low-pri task holds mutex; high-pri task waits → low-pri inherits high priority
- After unlock, low-pri priority restored to original
- Recursive lock with inheritance chaining
- Transitive PI: three-task chain (L→M→H) propagates boost
- Waiter priority change triggers re-evaluation of owner
- CONFIG_MUTEX_PIP=0 disables all PI behavior
- Edge: boost a task already holding another mutex (nested)
- Edge: multiple waiters at different priorities — owner inherits max
- Stress: rapid lock/unlock with random priorities

### Semaphore Priority Inheritance — test_semaphore_pip.cpp
- Low-pri task blocks on semaphore; high-pri task waits → low-pri inherits
- Priority restored after semaphore post
- Multiple waiters: owner inherits max waiter priority
- CONFIG_SEMAPHORE_PIP=0 disables all PI behavior
- Edge: semaphore with count > 1 — no boost if resource available

### Queue Priority Inheritance — test_queue_pip.cpp
- Boost sender when high-pri receiver waits on empty queue
- Boost receiver when high-pri sender waits on full queue
- Restore after send/receive completes
- CONFIG_QUEUE_PIP=0 disables all queue PI behavior
- Edge: simultaneous sender and receiver blocked
- Edge: multiple senders, single receiver

### Mutex Priority Ceiling Protocol — test_mutex_pcp.cpp
- Ceiling set > highest contender → no blocking on lock (immediate acquire)
- Ceiling set below contender → blocking occurs
- Held ceilings stack correctly on nested mutexes
- System ceiling prevents deadlock (protocol ceiling > lowest ceiling holder)
- Ceiling pop on unlock restores correct system ceiling
- CONFIG_PRIORITY_CEILING_PROTOCOL=0: PCP disabled, pure PIP
- Edge: ceiling == 0 (disabled per-mutex)
- Edge: held_ceiling_depth_ overflow protection

### Preemption Latency Tracking — test_preemption_latency.cpp
- CONFIG_PREEMPTION_LATENCY_MAX_CYCLES > 0 enables measurement
- Lock duration tracked as cycles
- Long critical sections trigger diagnostic
- No measurement when CONFIG_PREEMPTION_LATENCY_MAX_CYCLES == 0
