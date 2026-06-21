# Test Cases — v0.6.2 (Phase 7: Per-Task Software Watchdog)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Watchdog Create — test_task_watchdog_create.cpp
- `SYS_WATCHDOG_CREATE(period_ms)` returns valid watchdog ID
- Invalid period (0 or > 1h) returns EINVAL
- Maximum watchdogs per task enforced (returns ENOSPC)
- Watchdog is not armed on creation (explicit kick needed)

### Watchdog Kick — test_task_watchdog_kick.cpp
- `SYS_WATCHDOG_KICK(id)` resets timer for specific watchdog
- Expired watchdog (no kick within period) triggers action
- Kick on invalid watchdog ID returns EINVAL
- Kick on already-fired watchdog returns ESRCH

### Auto-Termination — test_task_watchdog_terminate.cpp
- Expired watchdog terminates the task
- Termination is clean (resources freed)
- Parent receives SIGCHLD with correct exit code
- Task in critical section (spinlock held) terminates safely

### /proc Export — test_watchdog_proc.cpp
- `/proc/[pid]/watchdog` lists all watchdogs for task
- Output format: `ID period_ms remaining_ms status`
- Status reflects: ARMING, ARMED, FIRED, DISABLED
- Reading /proc/[pid]/watchdog for another task returns EACCES
