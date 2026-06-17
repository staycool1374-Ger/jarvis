# Scheduler Stabilization & Hardening — Implementation Guide

**Status:** ACTIVE — Subordinate to `REFACTORING.md` executive override.
**Branch:** `main`
**Target version:** v0.2.13

---

## Codebase Baseline (as of v0.2.12)

### Scheduler Architecture
- Run queue: flat fixed-capacity pointer array `tasks_[64]`, O(n) linear scan on every tick (`next_task()`).
- Context switch driven by four `extern "C"` globals (`scheduler_save_rsp_to`, `scheduler_load_rsp_from`, `scheduler_load_cr3_from`, `scheduler_next_task_id`) shared between C++ and `isr_stubs.asm`.
- O(1) ID→TCB open-addressing hash table (128 slots, linear probing, tombstones).
- Scheduler runs entirely inside `handle_interrupt_c` (IF=0 guaranteed by CPU on ISR entry). No explicit `cli`/`sti` in C++ scheduler code.
- `preempt_enabled_` is a plain `bool` with no memory barrier — not safe as a standalone preemption guard.

### Synchronization Primitives (`src/kernel/sync/`)
| Primitive | File | Mechanism |
|---|---|---|
| `Mutex` | `mutex.cpp` / `mutex.hpp` | Priority-inheritance; fixed 32-waiter array; sets task `BLOCKED`, calls `Scheduler::reschedule()` |
| `Semaphore` | `semaphore.cpp` / `semaphore.hpp` | Counting; fixed 32-waiter array; same block/wake pattern |
| `Notify` | `notify.cpp` / `notify.hpp` | Single-waiter one-shot; sets state `READY` directly |
| `EventGroup` | `event_group.cpp` / `event_group.hpp` | 64-bit bitmask; 32 waiters with `wanted_bits` + `clear_on_exit` |
| `Queue` | `queue.cpp` / `queue.hpp` | Ring-buffer (32 slots × 32 bytes); separate send/receive waiter arrays |

### TCB Structure (`src/kernel/task/task.hpp`)
- Monolithic plain struct ~600+ bytes (debug: +832 bytes for `debug_switch_ring[4]`).
- Key fields: scheduling metadata, `TaskContext` (23 × uint64 = 184 bytes), kernel/user stacks, VFS `FdTable` + `cwd`, signal handlers, IPC objects, blocked-sender chain, process tree pointers, `uint64_t magic`.
- Debug ring buffer: `DebugSwitchRecord debug_switch_ring[4]` + `debug_switch_idx` — compiled only under `CONFIG_DEBUG`. Written exclusively in `switch_to_task()` for the outgoing task. Read only via GDB.

### Confirmed Active Bugs (Pre-Refactor)

**R1 — CRITICAL: Check-then-act race in all sync primitives**
All five primitives (`Mutex`, `Semaphore`, `Notify`, `EventGroup`, `Queue`) perform a check-then-act on ownership/state with no atomics and no IRQ guard. Example in `Mutex::lock()`:
```cpp
if (owner_ == nullptr) {   // <-- timer IRQ can fire here
    owner_ = task;         // <-- corrupting ownership
}
```
A preemption between the check and the assignment corrupts state on a uniprocessor with a preemptive timer.

**R2 — HIGH: Silent waiter overflow in all sync primitives**
`add_waiter()` returns `false` when the 32-slot array is full. All callers (`lock()`, `wait()`, etc.) ignore the return value. The task is neither blocked nor re-queued — it returns to its caller as if the operation succeeded. This is a silent correctness failure under contention beyond 32 concurrent waiters.

**R3 — HIGH: Unguarded scheduler mutation from non-ISR paths**
`reschedule()`, `add_task()`, `remove_task()` are called from non-ISR C++ paths (e.g., `Mutex::lock()` → `Scheduler::reschedule()`) with no IRQ save/restore. A timer tick re-entering `rate_monotonic_schedule()` mid-mutation corrupts `tasks_[]` and the hash table.

**R4 — MEDIUM: Open-coded `cli()`/`sti()` pairs (4 sites)**
`test_isolate.cpp` (3 sites) and `shell.cpp` (1 site) use the bare save/restore idiom:
```cpp
bool irq_was = arch::interrupts_enabled();
arch::cli();
/* critical section */
if (irq_was) arch::sti();
```
A `panic()` or unhandled path inside the critical section leaves interrupts permanently disabled.

**R5 — MEDIUM: No RAII interrupt guard exists**
No `IrqGuard`, `SpinLock`, `ScopedIrqDisable`, or equivalent RAII type exists anywhere in the codebase.

**R6 — LOW: No C++20 concepts on templates**
`align_up<T>`, `CheckedPtr<T>`, `safe_copy_from_user<T>`, `safe_copy_to_user<T>` accept any type. Wrong-type instantiations produce UB silently.

**R7 — LOW: No Thread-Safety Attribute annotations**
Zero `[[gnu::capability]]`, `[[gnu::guarded_by]]`, `[[gnu::scoped_lockable]]`, or related attributes anywhere in the codebase.

---

## Implementation Plan

### Phase A — Foundation: RAII `IrqGuard`
**Must be completed first. All subsequent phases depend on it.**

#### A.1 — Create `src/kernel/arch/irq_guard.hpp`

Implement a zero-overhead RAII guard that saves and restores RFLAGS.IF:

```cpp
/// @file irq_guard.hpp
/// @brief RAII interrupt state guard — saves RFLAGS.IF on construction,
///        restores it unconditionally on destruction.
///
/// Usage:
///   {
///       IrqGuard guard;   // saves IF, disables interrupts
///       // ... critical section ...
///   }                     // IF restored automatically
///
/// @note Nestable: inner guards save/restore correctly because each
///       instance stores its own saved state independently.
/// @note Must NOT be heap-allocated. Intended for stack use only.

#pragma once

#include <arch/io.hpp>

namespace kernel::arch {

class [[nodiscard]] IrqGuard {
public:
    IrqGuard() noexcept
        : irq_was_(arch::interrupts_enabled())
    {
        arch::cli();
    }

    ~IrqGuard() noexcept
    {
        if (irq_was_) arch::sti();
    }

    // Non-copyable, non-movable — stack-only semantics
    IrqGuard(const IrqGuard&)            = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;
    IrqGuard(IrqGuard&&)                 = delete;
    IrqGuard& operator=(IrqGuard&&)      = delete;

private:
    bool irq_was_;
};

} // namespace kernel::arch
```

#### A.2 — Retrofit existing open-coded cli/sti sites

Replace all 4 open-coded patterns with `IrqGuard`. Files:
- `src/kernel/test/test_isolate.cpp` — 3 sites (snapshot_create, snapshot_restore, cleanup path)
- `src/services/shell.cpp` — 1 site (`cmd_selftest`)

Pattern to replace:
```cpp
// BEFORE
bool irq_was = arch::interrupts_enabled();
arch::cli();
/* ... */
if (irq_was) arch::sti();

// AFTER
kernel::arch::IrqGuard guard;
/* ... */
// (guard destructs automatically at end of scope)
```

#### A.3 — Verification

Run `make test-qemu`. All pre-existing tests must pass. No behavior change expected — this is a mechanical refactor.

---

### Phase B — Core Fix: Sync Primitive Race Conditions (R1, R2, R3)

**Highest priority. Implements the directives in REFACTORING.md §1 and §2.**

#### B.1 — Guard `Scheduler::reschedule()`, `add_task()`, `remove_task()`

These three functions mutate shared scheduler state and are called from non-ISR paths. Wrap their entry with `IrqGuard`:

```cpp
// scheduler.cpp — add_task()
void Scheduler::add_task(TaskControlBlock* task) {
    IrqGuard guard;   // <-- add at top
    // ... existing body unchanged ...
}

// scheduler.cpp — remove_task()
void Scheduler::remove_task(uint64_t id) {
    IrqGuard guard;   // <-- add at top
    // ... existing body unchanged ...
}

// scheduler.cpp — reschedule()
void Scheduler::reschedule() {
    IrqGuard guard;   // <-- add at top
    // ... existing body unchanged ...
}
```

**Note:** `rate_monotonic_schedule()` and `on_tick()` run inside the ISR (IF already 0). Do NOT add a guard there — it would unconditionally disable interrupts on ISR exit (restoring to false instead of re-enabling).

#### B.2 — Fix check-then-act in all sync primitives (R1)

Wrap the ownership check-and-set in each primitive's critical method with `IrqGuard`. All five primitives follow the same structural pattern:

**`Mutex::lock()` in `mutex.cpp`:**
```cpp
void Mutex::lock(TaskControlBlock* task) {
    IrqGuard guard;
    if (owner_ == nullptr) {
        owner_ = task;
        return;
    }
    // ... waiter logic ...
}

void Mutex::unlock(TaskControlBlock* task) {
    IrqGuard guard;
    // ... existing wake logic ...
}
```

**`Semaphore::wait()` / `Semaphore::signal()` in `semaphore.cpp`:**
Same pattern — wrap both entry points.

**`Notify::wait()` / `Notify::notify()` in `notify.cpp`:**
Same pattern — wrap both.

**`EventGroup::wait()` / `EventGroup::set()` in `event_group.cpp`:**
Same pattern — wrap both.

**`Queue::send()` / `Queue::receive()` in `queue.cpp`:**
Same pattern — wrap both.

#### B.3 — Fix silent waiter overflow (R2)

`add_waiter()` currently returns `bool` but all callers ignore it. Two options:

**Option A (recommended) — Hard assert:**
```cpp
bool added = add_waiter(task);
ENSURE(added, "sync primitive waiter array full — MAX_WAITERS exceeded");
```
This panics the kernel immediately, making the bug visible rather than silently corrupting state.

**Option B — Propagate via `ErrorOr`:**
Change primitive return types to `ErrorOr<void>` and propagate to syscall handlers.

Use Option A for all five primitives. Document in `BUGS.md` if this triggers during testing.

#### B.4 — Verification

Run `make test-qemu` after each primitive is fixed (not all at once). Apply the 3-attempt circuit breaker from REFACTORING.md §2 Step 3 if a test regresses.

After all five are fixed, run a full suite pass and verify zero ResourceTracker deltas.

---

### Phase C — Compiler Enforcement: Thread-Safety Attributes

**Validates Phase B fixes via compile-time analysis. Run after B is complete.**

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

### Phase D — C++20 Concepts & `requires` Constraints

**Independent of Phases B/C. Can be worked in parallel or deferred.**

#### D.1 — Create `src/lib/concepts.hpp`

Define a minimal set of structural concepts for the kernel:

```cpp
/// @file concepts.hpp
/// @brief C++20 structural concepts for kernel template constraints.

#pragma once

#include <lib/utils.hpp>   // for type trait helpers

namespace kernel {

/// Satisfied by any integral arithmetic type (uint8_t through uint64_t, int variants).
template<typename T>
concept Integral = __is_integral(T);

/// Satisfied by types safe to copy byte-for-byte (no destructors, no vtable).
template<typename T>
concept TriviallyCopiable = __is_trivially_copyable(T);

/// Satisfied by a type that is not a reference (safe to store by value in containers).
template<typename T>
concept ValueType = !__is_reference(T);

/// Satisfied by a type usable as a kernel error enum (must be an enum class).
template<typename T>
concept ErrorEnum = __is_enum(T);

/// Satisfied by a type that provides lock() and unlock() (structural Lockable concept).
template<typename T>
concept Lockable = requires(T t) {
    { t.lock() };
    { t.unlock() };
};

} // namespace kernel
```

**Note:** Use `__is_integral`, `__is_trivially_copyable`, etc. (compiler built-ins) rather than `<type_traits>` — the kernel is freestanding with no STL.

#### D.2 — Retrofit template sites

| Template | File | Constraint to add |
|---|---|---|
| `align_up<T>` / `align_down<T>` | `lib/utils.hpp:103,113` | `requires kernel::Integral<T>` |
| `ErrorOr<T>` | `lib/error.hpp:26` | `requires kernel::ValueType<T>` |
| `CheckedPtr<T>` | `memory/checked_ptr.hpp:28` | `requires kernel::TriviallyCopiable<T>` |
| `safe_copy_from_user<T>` | `memory/checked_ptr.hpp:118` | `requires kernel::TriviallyCopiable<T>` |
| `safe_copy_to_user<T>` | `memory/checked_ptr.hpp:134` | `requires kernel::TriviallyCopiable<T>` |
| `checked<T>` factory | `memory/checked_ptr.hpp:82` | `requires kernel::TriviallyCopiable<T>` |
| `error_string<E>` specializations | `*_errors.hpp` files | `requires kernel::ErrorEnum<E>` |

Example retrofit:
```cpp
// BEFORE
template<typename T>
constexpr T align_up(T value, T align) { ... }

// AFTER
template<kernel::Integral T>
constexpr T align_up(T value, T align) { ... }
```

#### D.3 — Verification

Build must succeed with no new warnings. Existing template instantiation sites must all still compile (they all use valid types today). `make test-qemu` must pass.

---

## Execution Order & Dependency Graph

```
Phase A (IrqGuard)
    └──> Phase B (Sync Races)        [depends on A]
             └──> Phase C (Thread-Safety Attrs) [validates B]

Phase D (Concepts)                   [independent — run anytime]
```

**Recommended session sequence:**
1. **Session 1:** Phase A complete + Phase B.1 (scheduler guards) + `make test-qemu`
2. **Session 2:** Phase B.2–B.4 (all five sync primitives) + `make test-qemu`
3. **Session 3:** Phase C (thread-safety attributes + zero-warning build) + `make test-qemu`
4. **Session 4:** Phase D (concepts) + `make test-qemu` + commit + tag

---

## Testing Protocol

Follow REFACTORING.md §2 diagnostics protocol for every `make test-qemu` run:

1. If a test fails: inspect `debug_switch_ring` state via `make test-gdb` — extract `entry_addr`, `exit_rip`, `consumed_ticks` from the faulting task sequence.
2. Check ResourceTracker deltas — any non-zero delta after a test is a leak or double-free introduced by the refactor.
3. **Circuit breaker:** Max 3 consecutive fix attempts per failure. Halt and log to `BUGS.md` on 3rd failure.

### Tests Particularly Relevant to This Refactor

These test files exercise the scheduler and sync subsystem most directly — run them first when debugging regressions:

| Test File | Relevance |
|---|---|
| `test/test_scheduler.cpp` | Direct scheduler state transitions |
| `test/test_task.cpp` | TCB lifecycle, clone, cleanup |
| `test/test_ipc_blocking.cpp` | Mutex/Semaphore/Queue block/wake paths (R1, R2 exposure) |
| `test/test_sync.cpp` | All five sync primitives |
| `test/test_isolate.cpp` | Snapshot/restore — will change in Phase A.2 |
| `test/test_shell_interaction.cpp` | UART loopback — will change in Phase A.2 (`shell.cpp`) |

---

## Definition of Done

- [ ] `IrqGuard` implemented, all 4 open-coded `cli/sti` sites replaced
- [ ] `Scheduler::reschedule()`, `add_task()`, `remove_task()` guarded
- [ ] All 5 sync primitives: check-then-act wrapped with `IrqGuard`
- [ ] All 5 sync primitives: waiter overflow asserts with `ENSURE()`
- [ ] `-Wthread-safety` enabled, zero warnings on full build
- [ ] All sync primitives and `IrqGuard` annotated with capability attributes
- [ ] All shared scheduler state annotated with `[[gnu::guarded_by]]`
- [ ] C++20 concepts header created, 7 template sites retrofitted
- [ ] `make test-qemu` passes with zero failures and zero ResourceTracker deltas
- [ ] `BUGS.md` updated if any new bugs surfaced during refactor
- [ ] `LESSONS.md` updated if any non-trivial bugs were encountered
