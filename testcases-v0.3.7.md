# Test Cases — v0.3.7 (Phase 4: Configuration & Validation)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Hard-RT Config Profile — test_hard_rt_config.cpp
- `CONFIG_HARD_REAL_TIME=1` forces all dependent configs at build time
  - `CONFIG_PREEMPTION=1`, `CONFIG_MUTEX_PIP=1`
  - `CONFIG_STATIC_POOLS_ONLY=1`, `CONFIG_USE_APIC_TIMER=1`
- `CONFIG_HARD_REAL_TIME=0`: builds must remain functionally identical to v0.2.21 (Soft-RT compatibility)
- `CONFIG_WCET_ANALYSIS=1`: build with `-fstack-usage -ftime-report`, generates `wcet_report.txt`
- Config profile mismatch at compile time produces error (not silent misconfiguration)

### check-config Extensions — test_check_config.cpp
- Validate: `CONFIG_MAX_TASKS ≤ CONFIG_ID_TABLE_SIZE / 2`
- Validate: `CONFIG_STACK_SIZE × CONFIG_MAX_TASKS < CONFIG_KERNEL_HEAP_SIZE`
- Validate: `CONFIG_TICK_HZ` divides `CONFIG_TIMER_CLOCK_HZ` evenly (PIT/APIC)
- Validate: Sporadic Server C ≤ T for all configured servers
- Validate: Priority ceiling ≥ max task priority for each mutex
- Validate: `CONFIG_IRQ_LATENCY_MAX_NS < CONFIG_MIN_TASK_PERIOD_NS`
- Each validation failure produces a clear error message with value
- Validation runs at compile time (static_assert) where possible, else at init
