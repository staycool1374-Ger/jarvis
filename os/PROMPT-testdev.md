# Test Development Prompt
## Branch: testbed only — all test development and debugging happens here. Do not use on main.

### Testcases File
Before implementing, load `testcases-v<target-version>.md` for the current version's test ideas (e.g., `testcases-v0.2.10.md` for Phase 2 FAT32).

**Note on test-api:** Changing the test-api (registration, JARVIS_TEST macros, JARVIS_REGISTER_TEST, etc.) is not allowed. If in build mode, stop the implementation. If in plan mode, suggest the needed change.

### Test Sanctity Rule
All non-stub tests are read-only in the first instance. Only modify a non-stub test if it is systemically *wrong*. Changing a test requires first reading its `Testidea`/`Input`/`Expect`/`Depends` doc-block and its implementation; the doc-block and implementation must be changed together. Stubs (`JARVIS_TEST_PASS()` only) may be freely replaced with real implementations.

### Avoid write tool
Whenever writing into the filesystem, don't use the write tool, use the batch tool instead and pipe all data via small chunks (<50 lines).

### testbed Branch
All new tests are implemented on the `testbed` branch:
- **No production kernel code** — test files only. Everything goes into `src/kernel/test/`.
- **Order:** stub tests first, then replace stubs with real assertions, then verify with `make test-qemu`.
- **Stubs merge as-is:** Tests that need APIs not yet on `main` remain `JARVIS_TEST_PASS()` stubs after merging. The doc block documents what they should eventually assert.
- **Merge to main:** When all tests on `testbed` pass (0 failures), merge into `main`. `testbed` persists and accumulates tests for the next cycle — it is not deleted after merge.

### Test Design Principles (apply to all new tests)
0. **New tests are debug-only by default.** All new tests must use `JARVIS_REGISTER_TEST(name)` which places them in the debug target only. Only purely computational, zero-side-effect tests that have proven stable across many sessions may be promoted to `JARVIS_REGISTER_RELEASE_TEST(name)`. Release is a curated subset, not a default.
1. Boundary Testing: Test limit, limit-1, limit+1
2. Error Path Coverage: EFAULT, EINVAL, ENOSPC, EACCES, EBUSY, ENOENT
3. Unknown Input: Invalid message types, malformed structs, unknown syscalls
4. State Machine: Invalid transitions (TERMINATED to READY)
5. Resource Exhaustion: Max caps, max FDs, max tasks, full queues
6. Race/Concurrency: Multiple senders, writers, IRQ + thread
7. **Self-cleanup:** Every test must clean up its own resources. Tasks created with `add_task()` must be paired with `remove_task()` before `cleanup()`+`delete`. Any heap/PMM allocation must be freed. The runner warns on scheduler task-count leaks.
8. Cleanup on Failure: Partial init rollback, no leaks
8. Mock Interfaces: For hardware (PCI, Virtio, HPET, APIC) use mock

### Registration Pattern (in test_registry.cpp)
void register_task_lifecycle_tests();
void register_idle_task_tests();
void register_vfsd_tests();
void register_iocd_tests();
// All called in register_selftest_tests()

Note: `register_ipc_benchmark_tests()` is defined but never called from `register_selftest_tests()` — should be added or the benchmark file removed.

### Structural Issues (Historical — Resolved)
All structural issues (duplicate registrations, orphaned tests, overlapping tests) were fixed in a prior session. Registration functions are now unique, all `JARVIS_TEST()` entries are registered, and no duplicate tests exist.
