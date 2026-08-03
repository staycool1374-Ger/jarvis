# Test Cases — v0.3.12 (Alloc/Free Return-Value Audit)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

## Objective

Close every unhandled alloc/free return value in `src/kernel/**` found by the
2026-08-03 audit.  Groups A (null-deref on OOM), B (ignored return / leak), C
(double-free / stale-free).  Each item gets a deterministic verification test.

## Test Cases

### A — CRITICAL unchecked alloc (null/0 deref)

#### A1 — init_kstack_window OOM guard (task.cpp:344/355/366)
- **Testidea:** `PMM::alloc_page_table()` in the kslot window builder must not
  produce a phys-0 mapping or HHDM+0 write when it fails.
- **Input:** force `alloc_page_table()` to fail before the first `alloc_kslot()`.
- **Expect:** no null write; boot either panics cleanly or skips the mapping.
- **Depends:** `init_kstack_window`, `alloc_kslot`.

#### A2 — Scheduler::init idle-task null guard (scheduler.cpp:421)
- **Testidea:** `create(idle_task_main)` returning nullptr must not be deref'd.
- **Input:** inject a `TaskControlBlock::create()` OOM at `Scheduler::init`.
- **Expect:** early failure path (no `idle_task_->state` deref).
- **Depends:** `TaskControlBlock::create`, `Scheduler::init`.

#### A3 — Scheduler::reap idle recreation (scheduler.cpp:1448-1458)
- **Testidea:** when idle-task recreation fails, the old idle TCB is NOT freed
  and `idle_task_` stays valid.
- **Input:** force `create()` to fail in the reap path; trigger a reap cycle.
- **Expect:** no null deref; `get_idle_task()` still returns a live TCB.
- **Depends:** `Scheduler::reap`, `Scheduler::get_idle_task`.

#### A4-A6 — get_table null derefs in map paths (vmm.cpp)
- **Testidea:** `map_page` / `map_page_in_pml4` must null-check every
  `get_table(...)` level (RV64 l1/l2, x86_64 pt) and fail gracefully on OOM.
- **Input:** OOM-inject `get_table` during a user/brk/ELF mapping.
- **Expect:** mapping returns failure; no null write.
- **Depends:** `VMM::map_page`, `VMM::map_page_in_pml4`, `VMM::get_table`.

### B — HIGH / minor ignored returns and leaks

#### B1 — IPC::send checks BufferPool::transfer (ipc.cpp:240)
- **Testidea:** a failed `transfer()` must NOT leave a queued message whose
  `buf_handle` can be `map()`-ed by a receiver that isn't the owner.
- **Input:** send a message with a `buf_handle` that fails `transfer()` (bad
  handle / wrong owner).
- **Expect:** the message is not delivered (or the handle is cleared); no shared
  `buf_list_head` entry between sender and receiver.
- **Depends:** `IPC::send`, `BufferPool::transfer`, `BufferPool::map`.

#### B2 — exec_into_current leaks cloned PML4 on failure (elf.cpp:489)
- **Testidea:** when `load_segments_and_stack` fails, `new_pml4` and partial
  segments are freed (no PMM delta).
- **Input:** exec an ELF that fails mid-load.
- **Expect:** ResourceTracker PMM delta 0 after the failed exec.
- **Depends:** `elf::exec_into_current`.

#### B3 — create_user user-stack leak on clone failure (task.cpp:696-708)
- **Testidea:** if `clone_kernel_pml4()` fails, the pre-allocated user stack
  pages are freed (no PMM delta).
- **Input:** OOM-inject `clone_kernel_pml4()` during `create_user`.
- **Expect:** no user-stack page leak.
- **Depends:** `TaskControlBlock::create_user`, `PMM`.

#### B4-B5 — probe-failure leaks (virtio_net.cpp, ahci.cpp)
- **Testidea:** failed `VirtioNetDevice` probe and `AhciDriver::port_init`
  roll back every already-allocated page.
- **Input:** fail an allocation mid-probe.
- **Expect:** no PMM delta after the failed probe.
- **Depends:** `VirtioNetDevice`, `AhciDriver::port_init`.

#### B6 — ENSURE-on-OOM → graceful nullptr (vmm.cpp:139/227/289)
- **Testidea:** `get_table` huge-page split returns nullptr instead of panicking
  on OOM (consistent with the rest of `get_table`).
- **Input:** OOM-inject the huge-split `alloc_page_table()`.
- **Expect:** nullptr returned; caller handles it.
- **Depends:** `VMM::get_table`.

### C — double-free / stale-free

#### C1 — cleanup frees shared PML4 unconditionally (task.cpp:1275)
- **Testidea:** when `page_table_shared_` is true, `cleanup()` does NOT free the
  PML4 page.
- **Input:** create a task with `page_table_shared_ = true`, run `cleanup()`.
- **Expect:** PML4 page NOT freed; the sharing task can still clean it up once.
- **Depends:** `TaskControlBlock::cleanup`, `PMM::free_page` (double-free no-op
  mask is the risk — verify no reuse).

#### C2 — exec_into_current frees shared old_pml4 (elf.cpp:567)
- **Testidea:** `old_shared` guards the `PMM::free_page(old_pml4)` call.
- **Input:** exec with an `old_shared` true PML4.
- **Expect:** PML4 page not freed when shared.
- **Depends:** `elf::exec_into_current`.

#### C3-C4 — BufferPool pool_pages_ snapshot / slot scrub
- **Testidea:** `pool_pages_[]` is included in `capture_state`/`restore_state`
  AND `free_page` scrubs slots when the pool is full (no stale-foreign free).
- **Input:** run `buffer_pool_exhaustion` then a fresh alloc/free cycle.
- **Expect:** no stale `is_user_page` false-positive free; no foreign-page free.
- **Depends:** `BufferPool::capture_state/restore_state/free_page`.

## GDB / verification plan

- A1-A6: OOM-inject the alloc (temporarily return 0), run the affected class,
  confirm no null write (watch the target `pt`/`pdpt`/idle pointer).
- B1: set a breakpoint at `BufferPool::transfer` returning false in `IPC::send`;
  confirm the message is dropped.
- C1/C2: set `page_table_shared_`/`old_shared` true in a test task; verify
  `PMM::free_page(page_table_)` is not called.

## Acceptance

- A1-A6, B1, C1-C2 fixed and verified (build + class gate).
- B2-B5 no new ResourceTracker PMM deltas in `elf`/`process`/`driver`/`vfs`.
- `make build` clean (check-style Errors: 0); `selftest` 132/132.
- `test-history.txt` rows appended for every class touched.
- NOTE: `all` may still hang at ~test 78 on the H2 race (v0.3.9) — use
  per-class gates; keep `CONFIG_DEBUG_IPC_SCHED` ON for the debug `all` gate.
