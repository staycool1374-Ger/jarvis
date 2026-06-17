# Scheduler Stabilization & Hardening — Implementation Guide

**Status:** DEFERRED — Phases A, B, D complete. Only Phase C remains.
**Branch:** `main`
**Target version:** v0.2.13

---

## Remaining: Phase C — Compiler Enforcement: Thread-Safety Attributes

**Validates Phase B fixes via compile-time analysis.**

#### C.1 — Enable `-Wthread-safety` in the Makefile

Locate the `CXXFLAGS` definition in `Makefile` (or the relevant `mk/` fragment) and add:
```makefile
CXXFLAGS += -Wthread-safety
```

This will initially produce many warnings — that is expected and desired. Each warning is a concrete location where Phase B fixes may be incomplete.

#### C.2 — Annotate sync primitive classes

Add capability annotations to the five sync primitives. Example for `Mutex`:

```cpp
// mutex.hpp
class [[gnu::capability("mutex")]] Mutex {
public:
    void lock(TaskControlBlock* task)   [[gnu::acquire_capability()]];
    void unlock(TaskControlBlock* task) [[gnu::release_capability()]];
    bool try_lock(TaskControlBlock* t)  [[gnu::try_acquire_capability(true)]];
    // ...
};
```

Apply the same pattern to `Semaphore`, `Notify`, `EventGroup`, `Queue` with appropriate capability names (`"semaphore"`, `"notify"`, `"event_group"`, `"queue"`).

#### C.3 — Annotate shared scheduler data with `[[gnu::guarded_by]]`

In `scheduler.hpp` or the anonymous namespace in `scheduler.cpp`, annotate the shared state:

```cpp
// Requires a declared capability to use as the guard.
// Declare a dummy lock object for the scheduler's own data:
static kernel::arch::IrqGuard scheduler_irq_guard_ [[gnu::capability("mutex")]];

static TaskControlBlock* tasks_[MAX_TASKS]  [[gnu::guarded_by(scheduler_irq_guard_)]];
static uint64_t          task_count_        [[gnu::guarded_by(scheduler_irq_guard_)]];
static uint64_t          current_index_     [[gnu::guarded_by(scheduler_irq_guard_)]];
```

**Note:** Thread-safety analysis uses the declared capability object, not the type. `IrqGuard` should be annotated as `[[gnu::scoped_lockable]]` so the compiler understands its RAII acquire/release semantics:

```cpp
// irq_guard.hpp — update the class declaration
class [[nodiscard, gnu::scoped_lockable]] IrqGuard {
    IrqGuard() noexcept [[gnu::acquire_capability()]];
    ~IrqGuard() noexcept [[gnu::release_capability()]];
    // ...
};
```

#### C.4 — Resolve all `-Wthread-safety` warnings

Work through each warning produced. Expected categories:
1. Functions accessing `tasks_[]` without a guard → add `IrqGuard` (Phase B already covers these; this confirms completeness).
2. Functions annotated with `[[gnu::requires_capability(...)]]` being called without holding the capability → either add `IrqGuard` at the call site or add `[[gnu::locks_excluded(...)]]` where the function intentionally runs without a lock (ISR paths).
3. ISR-path functions (`rate_monotonic_schedule`, `on_tick`): annotate with `[[gnu::locks_excluded(scheduler_irq_guard_)]]` to document that they rely on the hardware IF=0 invariant rather than a software lock.

#### C.5 — Verification

Build must produce **zero** `-Wthread-safety` warnings. `make test-qemu` must pass.

---

## Testing Protocol

Follow REFACTORING.md §2 diagnostics protocol for every `make test-qemu` run:

1. If a test fails: inspect `debug_switch_ring` state via `make test-gdb` — extract `entry_addr`, `exit_rip`, `consumed_ticks` from the faulting task sequence.
2. Check ResourceTracker deltas — any non-zero delta after a test is a leak or double-free introduced by the refactor.
3. **Circuit breaker:** Max 3 consecutive fix attempts per failure. Halt and log to `BUGS.md` on 3rd failure.

---

## Definition of Done (Phase C only)

- [ ] `-Wthread-safety` enabled, zero warnings on full build
- [ ] All sync primitives and `IrqGuard` annotated with capability attributes
- [ ] All shared scheduler state annotated with `[[gnu::guarded_by]]`
- [ ] `make test-qemu` passes with zero failures and zero ResourceTracker deltas
- [ ] `BUGS.md` updated if any new bugs surfaced during refactor
- [ ] `LESSONS.md` updated if any non-trivial bugs were encountered
