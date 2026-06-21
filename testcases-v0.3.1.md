# Test Cases — v0.3.1 (Phase 4: High-Precision Timers & Deterministic Memory)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### HPET Driver — test_hpet.cpp
- HPET detected via ACPI HPET table
- HPET counter is monotonic and increasing
- HPET frequency matches expected value (typically 10+ MHz)
- One-shot timer fires at correct interval
- Periodic timer fires at correct interval
- HPET comparator value set/read correctly
- HPET legacy replacement mode disables PIT

### Deadline Monitoring — test_deadline.cpp
- Task deadline_ticks set correctly at creation
- Deadline overrun triggers registered callback
- Task with infinite deadline never overruns
- Multiple deadlines on same task (stacked)

### Periodic Release — test_periodic.cpp
- Periodic task releases at correct interval
- remaining_ticks resets after each release
- Release jitter within acceptable bound (< 100 us)
- Missed release (overrun) detected and reported

### WCRT Analysis — test_wcrt.cpp
- executed_ticks_max updates correctly
- Worst-case recorded across multiple runs
- Zero-length tasks (immediate exit) have minimal executed_ticks

### Scheduler Observability — test_sched_info.cpp
- `SYS_SCHED_INFO` returns valid task count
- `/proc/sched` exposes per-task metrics
- Reading /proc/sched from unprivileged task returns EACCES

### Deterministic User-space Pools — test_determ_pool.cpp
- Pre-allocated pool returns pages in O(1)
- Pool exhaustion returns error (no kernel fallback)
- Freed pages return to pool
- Pool statistics (used/free/total) accurate
- Concurrent allocation from pool does not corrupt
