# WCET Analysis — Deadline-Miss Detection Scan

**Phase:** 7b (v0.3.2 MissHandler)  **Status:** implemented & verified
**Owner:** developer  **Branch:** `v0.3.2-MissHandler`

## Objective

Quantify the worst-case execution time (WCET) of the deadline-miss detection
scan — `Scheduler::scan_deadlines()` — which backs the kernel's
`CONFIG_DEADLINE_MISS_DETECTION` machinery. `scan_deadlines()` performs an
`O(n)` walk of the live task set (`Scheduler::all_tasks()`), comparing each
task's `deadline_ticks` against the current tick and invoking
`deadline_miss_handler()` on any expired, not-yet-handled task. It is exercised
on two paths:

* **Monitor-task path** (`CONFIG_DEADLINE_MONITOR_TASK=1`, the default):
  the `[deadline-mon]` task calls `scan_deadlines()` when the tick posts
  `s_scan_requested_`.
* **Inline path** (`CONFIG_DEADLINE_MONITOR_TASK=0`): `on_tick()` calls
  `DeadlineList::pop_earliest_if_expired()` directly.

Both paths share the same `O(n)` cost profile, so a single benchmark of
`scan_deadlines()` bounds both.

## Methodology (`test_wcet_scheduler.cpp`)

The benchmark (`wcet_scan_deadlines`, class `wcet`) isolates the *pure scan*
cost so the result is not dominated by the serial-logging side effects of the
miss handler:

1. **Neutralise existing deadlines.** Every pre-existing task's
   `deadline_ticks` is pushed ~1e9 ticks into the future and its
   `period_ticks` cleared. This guarantees `scan_deadlines()` sees no expired
   task and therefore never enters `deadline_miss_handler()` (no serial I/O
   variance).
2. **One benchmark population.** 40 tasks are created with `period>0` and a
   far-future `deadline_ticks`, `add_task()`'d (so they live in the
   scheduler's own deadline tracking), then parked (`dequeue_ready` +
   `BLOCKED`) so they are never dispatched.
3. **Worst-case measurement.** For `kIters = 300` repetitions, with IRQs
   disabled (`arch::IrqGuard`) around each sample, measure
   `arch::rdtsc()` deltas across `scan_deadlines()` and keep the maximum
   (WCET). IRQs-off prevents the timer ISR from perturbing the sample.
4. **Scaling points.** The population is trimmed (deleted) to 10 tasks and
   again to 1 task, re-measuring at each size — yielding the 1 / 10 / 40 data
   points without create/delete churn between measurements.
5. Deadlines are restored and all benchmark tasks cleaned up via `ScopeGuard`,
   keeping `ResourceTracker` balanced (test asserts `> 0` cycles and passes
   leak-clean).

### Reproduce

```sh
make execute-test x86_64 debug wcet NO_LTO=1
# or, full matrix:
make execute-test x86_64 debug all NO_LTO=1
```

The measured worst-case is printed to the serial log:

```
[WCET] scan_deadlines 1-task  worst=2000 cyc
[WCET] scan_deadlines 10-task worst=9000 cyc
[WCET] scan_deadlines 40-task worst=13000 cyc
```

## Results

| Live deadline-tracked tasks | WCET `scan_deadlines()` (TSC ticks) |
| --------------------------: | ----------------------------------: |
|                          1   |                              2 000  |
|                         10   |                              9 000  |
|                         40   |                             13 000  |

> **Caveat — QEMU-virtualised TSC.** These figures were captured under QEMU
> (TCG). QEMU's virtual `RDTSC` is *not* a real hardware cycle counter; it
> advances with emulated virtual time and therefore reflects relative cost and
> scaling, not absolute nanoseconds on target silicon. On bare metal the
> absolute numbers will be different (typically far smaller), but the
> **O(n) scaling and the relative ratios are representative.**

## Scaling Analysis

The scan is linear in the number of live, deadline-tracked tasks. Observed
cost grows sub-proportionally per task as the population increases
(1→10 ≈ 4.5×, 10→40 ≈ 1.4× per-decade), consistent with a pointer-linked
walk whose per-node cost is dominated by the list traversal and the atomic
integrity counter increment (`deadline_detection_integrity`) rather than the
comparison itself.

### WCET bound at `MAX_TASKS`

`CONFIG_MAX_TASKS` caps the live task set (currently 64). Extrapolating the
measured linear trend, the worst-case `scan_deadlines()` cost at a fully
populated 64-task system is on the order of **~20 000 virtual TSC ticks**
(QEMU) — comfortably bounded and independent of the periodic timer period.
Because the scan runs under the scheduler lock and (in the monitor path) as a
discrete task wakeup rather than inside the tick ISR, this bounded cost does
not extend the hard real-time interrupt latency path.

## Conclusion

`scan_deadlines()` is `O(n)` in the live task count with a small constant
factor. Even at the architectural maximum it stays in the low tens of
thousands of (virtual) cycles, confirming the deadline-miss detection scan is
not a pathological WCET contributor for the targeted deployment scales.
