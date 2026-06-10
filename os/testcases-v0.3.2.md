# Test Cases — v0.3.2 (Phase 4: Budget Enforcement & Synchronization Protocols)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Static Resource Hierarchy (Deadlock Prevention) — test_lock_order.cpp
- Lock ordering enforced by resource ID
- Violation of lock order triggers assertion/panic in debug build
- Nested locks with correct order succeed

### Hardware Budget Enforcement — test_budget.cpp
- Timeslice budget `remaining_ticks` decrements each tick
- Budget exhaustion triggers preemption at tick boundary
- Task resumes with fresh budget next slice
- Budget=0 yields immediately

### Priority Inheritance Protocol (PIP) — test_pip.cpp
- Low-priority task holding lock inherits high-priority waiter's priority
- Priority restored to base after unlock
- Multiple waiters: highest inherited priority
- Nested inheritance: chain of 3 tasks
- No spurious inheritance when no contention

### Priority Ceiling Protocol (PCP) — test_pcp.cpp
- Mutex ceiling priority set at init
- Task with priority > ceiling locks successfully
- Task with priority < ceiling blocks on lock
- Ceiling protocol prevents deadlock (verifiable by lock ordering)

### Async Lock Validation — test_lock_validator.cpp
- Debug-build validator detects cyclic lock request
- Cycle detection reports all tasks in cycle
- Valid (acyclic) lock sequence passes validator
- Validator overhead measured (< 100 us per check)

### Memory Pinning — test_mlock.cpp
- `SYS_MLOCK` pins page range in RAM
- `SYS_MLOCKALL` pins all task pages
- Pinned pages are not swapped (if swap exists)
- Double-lock is idempotent
- `SYS_MUNLOCK` releases pin
- Unlocking non-pinned page returns EINVAL
- Locking invalid address range returns ENOMEM
