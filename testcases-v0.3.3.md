# Test Cases — v0.3.3 (Phase 4: WCET Analysis & Tuning)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Syscall WCET Tracking — test_syscall_wcet.cpp
- Each syscall records min/max/avg execution time
- Fast syscalls (getpid, getticks) have sub-microsecond WCET
- Blocking syscalls (recv, send) record active time only
- WCET stats exported via /proc/syscall_stats

### Kernel Stack Usage Profiler — test_stack_profiler.cpp
- Background task records max stack depth per thread
- Stack depth increases with call nesting
- Stack usage resets on thread exit
- Profiler overhead does not skew measurements

### Allocation-free IRQ Paths — test_irq_alloc.cpp
- No dynamic allocation in any IRQ handler
- No allocation in syscall fast-path (getpid, getticks)
- Allocation only in syscall slow-path (open, fork) where documented

### Interrupt Latency Measurement — test_irq_latency.cpp
- Hardware-timed interrupt response time measured
- Latency distribution: min, max, avg, p99
- No interrupt latency exceeds 10 us on idle system
- Worst-case latency under load bounded

### Jitter Benchmarking — test_jitter.cpp
- Synthetic load: N tasks at same priority
- Schedule-to-schedule jitter measured
- Jitter is deterministic (bounded variance)
- Jitter under max load < 2x idle jitter
