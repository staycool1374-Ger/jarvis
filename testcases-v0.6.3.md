# Test Cases — v0.6.3 (Phase 7: Deadlock Detection & Recovery)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Wait-For-Graph — test_wfg.cpp
- WFG constructed from locked resources and blocked tasks
- Single dependency: A waits for B -> directed edge A->B
- WFG updated atomically on lock/unlock
- WFG prunes resolved dependencies

### Deadlock Detection — test_deadlock_detect.cpp
- Two-task circular wait detected as deadlock
- Three-task cycle (A waits B, B waits C, C waits A) detected
- Nested mutex deadlock detected
- No false positive on valid lock ordering
- Detection completes in bounded time (O(tasks + resources))
- Detection overhead < 100 us per check

### Recovery — test_deadlock_recovery.cpp
- Deadlocked task forcibly terminated
- Terminated task's locks released
- Waiters of terminated task unblocked
- Resources of terminated task reclaimed
- Survivors continue without corruption
- Same deadlock not immediately re-formed after recovery

### Health Status — test_health.cpp
- `SYS_HEALTH_STATUS` returns system health metrics
- Fields: deadlock_count, recovered_count, watchdog_firings
- Health counter monotonically increasing
- Privileged-only: unprivileged caller gets EACCES
- `/proc/health` exposes same data in human-readable format
