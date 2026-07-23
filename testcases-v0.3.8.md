# Test Cases — v0.3.8 (Phase 4: Test & Verification Suite)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### Infrastructure — test_infra.cpp
- `vfs_touched` flag: lazy daemon restart skips reload for non-VFS tests (~70-80% overhead reduction)
- Snapshot/restore preserves daemon state when `vfs_touched` not set
- Disabled/stub test remediation: triage ~90+ disabled/stub/broken tests
- Re-enable or remove disabled tests (`#if 0`, registration-commented)

### Syscall WCET Tracking — test_syscall_wcet.cpp
- Each syscall records min/max/avg execution time
- Fast syscalls (getpid, getticks) have sub-microsecond WCET
- Blocking syscalls (recv, send) record active time only
- WCET stats exported via /proc/syscall_stats

### WCET Measurement — test_wcet_scheduler.cpp
- Measure `next_task()`, `reschedule()`, `switch_to_task()` cycles (10k iterations)
- Record in `docs/wcet_analysis.md`

### WCET Measurement — test_wcet_ipc.cpp
- Measure send/receive latency (same core, cross-core via IPI)
- Record in `docs/wcet_analysis.md`

### WCET Measurement — test_wcet_interrupt.cpp
- Measure IRQ entry→handler→exit latency (histogram)
- Record in `docs/wcet_analysis.md`

### WCET Measurement — test_wcet_memory.cpp
- Measure `MemPool::alloc/free`, `VMM::map/unmap` cycles
- Record in `docs/wcet_analysis.md`

### Priority Inversion — test_priority_inversion_mutex.cpp
- Classic 3-task inversion: low holds mutex, medium preempts, high waits
- Verify PIP bounds blocking to bounded duration (not indefinite)
- Without PIP (CONFIG_MUTEX_PIP=0): inversion duration is unbounded

### Priority Inversion — test_priority_inversion_semaphore.cpp
- Same 3-task inversion pattern for binary semaphore
- Verify PIP bounds blocking

### Priority Inversion — test_priority_inversion_queue.cpp
- Same 3-task inversion pattern for message queue
- Verify PIP bounds blocking

### Chained Blocking — test_chained_blocking.cpp
- 5-task chain with PCP: verify PCP prevents deadlock
- Without PCP: circular wait forms; with PCP: ceiling blocks the cycle

### Interrupt Latency — test_irq_latency_histogram.cpp
- Inject synthetic IRQs, verify ≤ `CONFIG_IRQ_LATENCY_MAX_NS`

### Interrupt Latency — test_nested_irq_latency.cpp
- Nested ISRs, measure tail-chaining overhead

### Memory Determinism — test_no_dynamic_alloc_after_init.cpp
- Scan for PMM/VMM allocations post-init — zero dynamic allocation

### Memory Determinism — test_stack_guard_pages.cpp
- Overflow triggers #PF → `CONFIG_STACK_OVERFLOW_HOOK` called

### Memory Determinism — test_static_pool_exhaustion.cpp
- Exhaust each pool, verify graceful failure (task blocked/killed, capacity restored)

### Multi-Arch CI
- `make test-all-hard-rt ARCH=x86_64` — all above tests pass
- `make test-all-hard-rt ARCH=aarch64` — via Renode (when HAL ready)
- `make test-all-hard-rt ARCH=riscv64` — via Renode (when HAL ready)
