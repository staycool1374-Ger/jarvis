# Test Cases — v0.4.2 (Phase 5: Per-CPU Scheduling & Load Balancing)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Distributed Run Queues — test_percpu_sched.cpp
- Each CPU has independent run queue
- Task enqueued on one CPU does not appear on another's queue
- Lock contention on run-queue operations is eliminated
- N tasks on M CPUs: each CPU serves its own queue

### Load Balancing — test_load_balancer.cpp
- Idle CPU pulls tasks from busy CPU's queue (idle-pull)
- Overloaded CPU pushes tasks to idle CPU (work-push)
- Load balancer does not migrate real-time tasks
- Migration threshold prevents thrashing
- Load balanced within 10% deviation across CPUs

### Core Affinity — test_affinity.cpp
- `SYS_SET_AFFINITY` pins task to specific CPU
- `SYS_GET_AFFINITY` returns current CPU mask
- Pinned task never runs on non-affine CPU
- Affinity mask with no CPUs online returns EINVAL
- sched_setaffinity/sched_getaffinity wrappers in libc

### Cache Coloring — test_cache_coloring.cpp
- Consecutive allocations get different cache colors
- Cache color collisions minimized for threaded access
- Coloring algorithm handles arbitrary allocation sizes
- Page address determines color (bit extraction)

### SMP Synchronization Primitives — test_smp_sync.cpp
- SMP spinlock: multiple CPUs, lock/unlock, no data race
- Reader-writer lock: concurrent readers, exclusive writer
- Spinlock with IRQ save/restore works on all CPUs
- Lock holder CPU migration does not corrupt lock
- Ticket lock: FIFO ordering under contention

### SMP Verification — test_smp_verify.cpp
- Single-core WCET/WCRT bounds hold on SMP
- Lock contention adds bounded latency
- No priority inversion across CPUs
- 24h stress: all CPUs, random fork/exit/IPC
