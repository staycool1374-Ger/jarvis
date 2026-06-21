# EXECUTIVE OVERRIDE: SCHEDULER REGRESSION & STABILIZATION MODE
**Status:** PARTIALLY COMPLETE — Phases A (IrqGuard), B (sync race fixes), and D (C++20 concepts) implemented. Phase C (thread-safety attributes) deferred.
**Target System:** Preemptive Scheduler & Context-Switch Subsystem.
**Core Directive:** Systematically isolate, expose, and eliminate race conditions, jitter, or state regressions in the preemption engine. Stabilize the current preemptive foundations to ensure a rock-solid baseline before any Phase 4 $O(1)$ architectural overhauls.

## 1. Stabilization Guardrails (Strict)
- **IRQ / Preemption Integrity:** Every modification to code path synchronization must explicitly guarantee invariant interrupt states. If a routine requires an internal lock, verify interrupt flags are saved and restored properly (`RFLAGS` preservation).
- **No Optimization Creep:** Do not prematurely implement Phase 4 bitmap queues or deadline metrics. Focus entirely on stabilizing the correctness, determinism, and safety of the *existing* preemption loops.
- **Asynchronous Invariant Checks:** Ensure that asynchronous context switches do not bypass or corrupt `kernel::test::ResourceTracker` task accounting or the context-switch trace ring buffers (`debug_switch_ring`).

## 2. Targeted Diagnostics & Testing Loop
When assessing or modifying the scheduler, follow this specific diagnostics protocol:

### Step 1: Preemption Stress Test Mapping
Before modifying any core scheduling files (`scheduler.cpp`, `task.cpp`), analyze how the automated test suite simulates asynchronous scheduling. Look for tests that exercise:
- Rapid task state transitions (Ready -> Blocked -> Ready).
- Interrupt-driven involuntary yields vs. explicit `SYS_YIELD` calls.
- Deep nested kernel-space preemptions.

### Step 2: Diagnostic Verification
If a test fails or a regression is detected:
- Immediately inspect the `debug_switch_ring` state using the GDB panic surveillance targets (`make test-gdb`) to extract `entry_addr`, `exit_rip`, and `consumed_ticks` of the faulting task sequence.
- Check for page-table leaks or memory corruption if the failure involves `clone()` or parent-child PML4 space isolation.

### Step 3: Atomic Bug Regression Logging
Any isolated scheduler bug must be instantly documented in `BUGS.md` with detailed execution context (e.g., whether the fault occurred under heavy UART loopback FIFO strain or VFS daemon task context switches). Do not clear the bug until it passes a minimum 50-iteration continuous QEMU validation run.
