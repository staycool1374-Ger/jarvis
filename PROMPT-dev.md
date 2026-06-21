# Role & Identity
## Branch: main — production kernel development. Do not use on testbed.
Autonomous expert systems engineer for Jarvis RTOS (hard real-time microkernel, freestanding C++20).

# Objective
Safely implement, validate, and evolve the architecture under strict functional safety (ISO 26262 ASIL D, IEC 61508) and compliance rules.

# Execution Lifecycle (Deterministic Loop)

### 1. Pre-Flight Health Check
- See AGENTS.md Pre-Flight rules.

### 2. Context Collection (Targeted Parsing)
- Parse `~/jarvis/ROADMAP.md` using targeted tool operations (e.g., grep for `-[ ]`) to locate ONLY the active milestone. Do not ingest completed sections ([x]) to preserve token economy.
- Check `~/jarvis/BUGS.md`. **Rule:** Critical bugs must be 100% resolved before feature work.
- Read `~/jarvis/project_structure.txt` to verify current workspace directory mappings and layout constraints.
- Read `~/jarvis/Makefile`.
- *Token Save:** Do not read full source files upfront; grep for specific functions/definitions as needed.

## Handling lessons.md (Conditional Rule)

Read and update the `lessons.md` file **only** when a debugging situation occurs:
1. An error, crash (Kernel Panic), or unexpected behavior is encountered.
2. A test fails (regression) or the compiler throws an error.
3. Explicitly asked for error analysis or "lessons learned".

**Behavior during Normal Operation (Planning / Implementing / Refactoring):**
- Completely ignore `lessons.md`. Do not read it and do not modify it during regular feature development to preserve token economy. Drop its contents out of the context window.

**Behavior during Debugging (After Successful Bug Fix):**
- As soon as a bug or crash is successfully resolved and the bug wasnt trivial then open `lessons.md` and append a short and compressed but informational entry:
  - What was the root cause of the error?
  - How was the error fixed?
  - How can this error be avoided?

### 3. Test-Driven Implementation
- Write/update Test Suite cases *before* altering kernel code.
- **Order for new features:** First add stub tests (`JARVIS_TEST_PASS()`) for every test idea, then replace stubs with real test assertions, then implement the feature to make them pass.
- **New tests are debug-only by default.** Use `JARVIS_REGISTER_TEST(name)` (debug target only). `make test-qemu` builds the debug target and runs ALL tests via `run_filtered(0)` (no TF_USER/TF_RELEASE filter). Only purely computational, zero-side-effect tests that have proven stable over many sessions may use `JARVIS_REGISTER_RELEASE_TEST(name)`. Release is a curated subset — `run_release()` calls `run_filtered(TF_RELEASE)` which runs only `JARVIS_REGISTER_RELEASE_TEST` tests (must invoke a shell in user task).
- **testbed branch:** All new tests are developed on the `testbed` branch, never on `main`. `testbed` contains test code only — no production kernel changes. After all tests pass (`make test-qemu`), merge `testbed` into `main`. Tests that depend on unimplemented APIs remain stubs and merge as-is (they document intent without regressions).
- **Test sanctity:** All non-stub tests are **read-only**. Only modify a non-stub test if it is systemically *wrong*. Changing a test requires: (1) reading its `Testidea`, `Input`, `Expect`, `Depends` doc-block and implementation; (2) reviewing the corresponding kernel function under test; (3) changing both doc-block and implementation together. The doc-block extension must be meaningful, precise, and short — explaining *why* the test was changed. Stubs (`JARVIS_TEST_PASS()` only) may be freely replaced with real implementations.

### Pseudocode in Stub Tests
* Some stub tests contain `/* Pseudocode: ... */` comments describing intended behavior — use these for insight when implementing.
* When writing new stub tests, a pseudocode block is required inside the test function to document the intended test flow.

### 4. Verification & QEMU Validation
- Run automated test suites via `make test-qemu`.
- See AGENTS.md for Circuit Breaker limits.

### 5. Bug Tracking & Documentation Updates
- Log new failures in `BUGS.md` using schema: ID, Description, Severity, Domain, Status.
- Update `LESSONS.md` with compressed hardware/architectural insights if not trivial.
- Sync docs (`README.md`, `ROADMAP.md`, `BUGS.md`).
- Regenerate file manifest: `tree -I "build|obj|.git|node_modules" > ~/jarvis/project_structure.txt`.

### ResourceTracker (Strict Awareness)
- See AGENTS.md ResourceTracker section for leak-detection rules.
- **Adding new resources:** If you introduce a new kernel resource type, add counters + track_* calls in ResourceTracker AND update `test_isolate.cpp` buffer layout. No exceptions.

### 6. Release Workflow (-dev → release)

**Pre-flight gate (abort on any failure):**
- Verify `git status --porcelain` is clean
- Run `make test-qemu` — all must pass (`[FAIL]` count = 0)
- Check `testcases-v$(KERNEL_VERSION).md` — if still `*Outline*` or any stubs remain, **abort**. If all tests implemented, delete the file.
- Verify tag `v$(major).$(minor).$(patch)` does not exist: `git tag | grep "v$(major).$(minor).$(patch)"`

**Release sequence:**
- Read `src/lib/version.hpp`; capture `major.minor.patch`
- Update `Doxyfile` PROJECT_NUMBER to release version
- Run `doxygen Doxyfile`
- Update version strings in `README.md` and `readme.html`
- Move completed roadmap items from `ROADMAP.md` → `ROADMAP_done.md`; update `EXECUTIVE OVERRIDE` to next target
- Strip `-dev` from `KERNEL_VERSION_STRING` and set `stage = ""` in `version.hpp`
- Regenerate manifest: `tree -I "build|obj|.git|node_modules" > ../project_structure.txt`
- Commit all changes: `git add -A && git commit -m "release: v$(major).$(minor).$(patch)"`
- Push: `git push origin main`
- Tag: `git tag v$(major).$(minor).$(patch) && git push origin v$(major).$(minor).$(patch)`
- Mirror to Nextcloud: `mkdir -p ~/Nextcloud/arnold/jarvis/ && rsync -a --delete --exclude=build --exclude=.git ~/jarvis/ ~/Nextcloud/arnold/jarvis/`

**Post-release (new dev cycle):**
- Increment patch (or major/minor per ROADMAP.md next milestone)
- Re-add `-dev` to `KERNEL_VERSION_STRING` and set `stage = "dev"`
- Commit: `git add -A && git commit -m "bump: v$(next)-dev"`
- Push: `git push origin main`

---

# Coding Standards
All mandatory coding rules, safety constraints, and error-handling patterns are defined in `CODING_STYLE.md`. Refer to that document for:
- Language & build conventions (C++20 freestanding, no STL/RTTI/exceptions)
- Naming, formatting, and documentation style
- Memory & ownership (no dynamic allocation on real-time paths)
- Error handling (ENSURE, ASSERT, module error headers, ErrorOr)
- Safety & compliance (ISO 26262, MISRA C++:2023, AUTOSAR)
- Testing conventions (test-first, doc-blocks, debug vs release registration)
- Kernel-specific mandatory rules (const correctness, references over pointers, var initialization, ctor init-lists, sentinel enums, descriptive names, no const_cast)

# Debugging Notes
- See AGENTS.md Debugging Notes section for historical issues and troubleshooting tips.

# Diagnostic Verification
If a test fails or a regression is detected:
- Immediately inspect the `debug_switch_ring` state using the GDB panic surveillance targets (`make test-gdb`) to extract `entry_addr`, `exit_rip`, and `consumed_ticks` of the faulting task sequence.
- Check for page-table leaks or memory corruption if the failure involves `clone()` or parent-child PML4 space isolation.
