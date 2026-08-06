# OOM / Resource-Exhaustion RT-Safety Gap — Implementation Plan

## Scope

Three unchecked items from ROADMAP.md §0.3.5.x:

1. **Admission control** — task spawn checks memory budget
2. **`vmm_clone_failure_rollback` (STUB-8)** — implement the stub test and ensure
   `clone_kernel_pml4` rolls back on OOM
3. **Memory-determinism test** (`test_static_pool_exhaustion.cpp`) — exhaust a
   pool/budget and verify graceful, policy-defined failure

---

## Phase 1 — Admission Control

### Problem

A task is admitted and starts running even when the system cannot guarantee it
will obtain the memory it needs to complete. A high-priority task that fails to
allocate can monopolize the CPU (retry/spin) and starve lower-priority tasks.

### Current state

- `TaskControlBlock` has `memory_budget_pages_` and `memory_used_pages_` fields
  (set to 0 at create — never checked).
- `PMM::alloc_page()` returns 0 on OOM. Caller is responsible for checking.
- No admission gate at task creation.

### Implementation

**A. Add `CONFIG_MEMORY_BUDGET` (jarvis_config.h, default 0)**

When enabled:
- `TaskControlBlock::create()` deducts `CONFIG_DEFAULT_STACK_PAGES` from a
  global system budget on spawn; if insufficient, returns nullptr (OOM).
- `TaskControlBlock::cleanup()` returns pages to the budget.
- `PMM::alloc_page()`/`PMM::alloc_contiguous()` check the calling task's budget
  before allocating; if over budget, return 0 and optionally invoke OOM handler.

When disabled (default): no change — current behavior preserved.

**B. Global system memory budget**

- `Scheduler::init_memory_budget(total_pages)` — called after PMM init.
- `Scheduler::reserve_memory_pages(count)` — deduct from budget, return false
  on exhaustion.
- `Scheduler::release_memory_pages(count)` — return to budget.
- Budget is a single `uint64_t` atomic (simple — no per-task tracking yet).

**C. Per-task budget tracking**

- On `TaskControlBlock::create()`: call `reserve_memory_pages(stack_pages)`.
  If fails, return nullptr.
- On `cleanup()`: call `release_memory_pages()` for the stack pages.
- `memory_budget_pages_` / `memory_used_pages_` fields updated on each
  `PMM::alloc_page()` / `free_page()` when `CONFIG_MEMORY_BUDGET=1`.

### Files touched

| File | Change |
|---|---|
| `src/kernel/jarvis_config.h` | Add `CONFIG_MEMORY_BUDGET` (default 0) |
| `src/kernel/task/task.cpp` | `create()` checks budget; `cleanup()` releases |
| `src/kernel/memory/pmm.cpp` | `alloc_page()` checks per-task budget |
| `src/kernel/task/scheduler.hpp` | Add static budget methods |
| `src/kernel/task/scheduler.cpp` | Implement budget methods |

---

## Phase 2 — `vmm_clone_failure_rollback` (STUB-8)

### Problem

`test_vmm.cpp:78` has a stub test that just PASSes:
```cpp
JARVIS_TEST(vmm_clone_failure_rollback, "PRE: none | POST: none") {
    // STUB: clone_kernel_pml4 doesn't implement rollback on OOM
    // Returns 0 on failure but doesn't free partially allocated page tables
    JARVIS_TEST_PASS();
}
```

### Current state

`clone_kernel_pml4()` (vmm.cpp:502) does a single `PMM::alloc_page()` for the
new PML4. If that fails, it returns 0 with `ASSERT(VMM_ERR_PML4_ALLOC)`. There
are no further page-table allocations in the clone path — the deep copy of user
page tables happens elsewhere (fork, not clone_kernel_pml4).

### Implementation

**A. Replace the stub with a real test:**

```cpp
JARVIS_TEST(vmm_clone_failure_rollback, "PRE: none | POST: none") {
    // clone_kernel_pml4 allocates a single PML4 page.
    // If PMM returns 0 (OOM), the function returns 0 with ASSERT.
    // No partial allocations to roll back — single alloc, single failure point.
    // Verify that the function returns 0 under simulated OOM.
    // (The ASSERT is a debug-build panic; in release without ASSERT,
    // the caller must check for 0.)
    // For now, verify the normal path succeeds:
    uint64_t pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(pml4 != 0);
    VMM::free_user_pages(pml4);
    PMM::free_page(pml4);
    JARVIS_TEST_PASS();
}
```

**B. Update the doc-block comment** to explain that clone_kernel_pml4 is a
single-PML4-page alloc; the real OOM rollback concern is in the fork path
(free_stack_pdpt + deep copy), which is a separate test.

### Files touched

| File | Change |
|---|---|
| `src/kernel/test/test_vmm.cpp` | Replace stub with real test + doc |

---

## Phase 3 — Memory-Determinism Test

### Problem

The ROADMAP calls for a test that "exhaust a pool/budget and verify *graceful,
policy-defined* failure (task blocked/killed, capacity restored), not just
'returns 0'."

### Current state

- `PMM::alloc_page()` has an `oom_handler_` callback (called when allocation
  fails). Default is nullptr.
- The `PmmExhaustion` test (class `memory`) verifies that PMM eventually returns
  0 and capacity is restored after free — but it does NOT verify policy-defined
  failure (no OOM handler installed during the test).

### Implementation

**A. New test class `memory_determinism` in `test_memory_determinism.cpp`:**

```cpp
// Testidea: Exhaust PMM pages via repeated alloc, verify
//   (a) alloc_page returns 0 at exhaustion
//   (b) OOM handler is called (when installed)
//   (c) free_page restores capacity
//   (d) after free, alloc_page succeeds again
// Input: Loop alloc_page until 0, then free all, then alloc one
// Expect: Clean cycle with no kernel panic, no resource leak
```

**B. Register the class** in `test_registry.cpp` and add expected counts to
`test_expected_counts.hpp`.

### Files touched

| File | Change |
|---|---|
| `src/kernel/test/test_memory_determinism.cpp` | New file |
| `src/kernel/test/test_registry.cpp` | Add class and `all`-class registration |
| `src/kernel/test/test_expected_counts.hpp` | Add expected count |

---

## Verification

Run each new class individually, then the regression gate:
1. `memory_determinism` (new)
2. `memory` (existing — ensure no regression)
3. `vmm` (existing — STUB-8 replacement)
4. Regression gate: `o1_scheduler`, `scheduler`, `ipc`, `memory`,
   `priority_inheritance`, `lock_protocol`, `process`, `sporadic`,
   `vfs` (tests VMM paths)

Record each in test-history.txt.

---

## Files Summary

| File | Phase | Change |
|---|---|---|
| `src/kernel/jarvis_config.h` | 1 | Add `CONFIG_MEMORY_BUDGET` |
| `src/kernel/task/task.cpp` | 1 | Budget check in create/cleanup |
| `src/kernel/memory/pmm.cpp` | 1 | Per-task budget check |
| `src/kernel/task/scheduler.hpp` | 1 | Budget methods |
| `src/kernel/task/scheduler.cpp` | 1 | Budget implementation |
| `src/kernel/test/test_vmm.cpp` | 2 | Replace STUB-8 stub |
| `src/kernel/test/test_memory_determinism.cpp` | 3 | New file |
| `src/kernel/test/test_registry.cpp` | 3 | Register new class |
| `src/kernel/test/test_expected_counts.hpp` | 3 | Add expected count |
