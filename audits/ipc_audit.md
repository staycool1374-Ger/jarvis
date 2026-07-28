[>] Running Agent 3: Kernel Synthesizer...
# JARVIS RTOS — IPC/Sync Module Audit — Verified Findings
## Lead Kernel Architect Disposition of Attacker Report (Agent 2)

Machine-readable directive for automated remediation. Findings below have been cross-checked against the actual source. Unverifiable or architecturally-intentional claims have been rejected (see REJECTED section). Only findings with a concrete, line-traceable root cause are carried forward as actionable tickets.

---

## REJECTED (False Positive) — DO NOT ACTION

| Attacker ID | Reason for Rejection |
|---|---|
| C-1 (as framed) | Mischaracterized as generic "livelock." The spin-wait under `arch::interrupts_enabled()` is bounded by one timer-tick period under the documented INV-4 deferred-reschedule model — this is intentional and bounded. However, a **real** defect was found hiding inside this function under the *interrupts-disabled* branch — re-filed as `VULN-IPC-01` below. |
| C-3 | "O(1) wake-up" is not a cited invariant in the provided source. `MAX_WAITERS`/`CONFIG_SYNC_MAX_WAITERS` is a small fixed compile-time bound; linear scan over it is deterministic, bounded WCET — compliant with ASIL-D timing requirements. No fix required. |
| C-4 | Unsubstantiated: no call site in the provided files invokes `EventGroup`/`Notify` from ISR/interrupt-handler context. `SpinLock` is used identically by `Mutex`/`Semaphore`/`Queue` under the same cooperative model. Speculative without a demonstrated ISR call path — rejected pending evidence. |
| H-2 | Attacker self-identifies this as "not a live bug in these files." No action. |
| M-1 | Explicitly a compliance confirmation, not a defect. |
| M-3 | Redundant restatement of H-4; folded into `VULN-SYNC-03` below. |

---

## VERIFIED FLAWS

- [ ] **VULN-IPC-01**
- **FILE/FUNCTION:** `src/kernel/ipc/ipc.cpp`, `IPC::send()` — the block:
  ```cpp
  block_sender(tcb->msg_queue, *cur);
  cur->state = TaskState::BLOCKED;
  Scheduler::reschedule();
  if (arch::interrupts_enabled()) { while (cur->state == TaskState::BLOCKED) arch::pause(); }
  // falls through here immediately if interrupts were disabled
  tcb = Scheduler::find_task(dest_id);
  ...
  if (tcb->msg_queue.is_full()) return false;
  ```
- **ROOT CAUSE:** When `IPC::send()` is invoked with interrupts disabled, `block_sender()` has already (a) set `cur->state = BLOCKED`, (b) called `Scheduler::dequeue_ready(cur)`, and (c) linked `cur` into `tcb->msg_queue.blocked_senders_head`. The `if (arch::interrupts_enabled())` guard causes the wait loop to be **skipped entirely**, so the function falls straight through to the full-queue re-check and returns `false` — but **never rolls back** the state mutation performed by `block_sender()`. The caller resumes execution believing `send()` simply failed, while the TCB is left `BLOCKED` + off the ready queue + linked into a foreign wait list. The task is now a live thread of execution masquerading as a blocked/dequeued task — a violation of the WEDGE invariant's spirit and a guaranteed future use-after-logic-state corruption when `wake_sender()` eventually calls `Scheduler::set_task_ready(*cur)` on a TCB that has since moved on to unrelated work (or been reused). This is a non-recoverable liveness/consistency defect — unacceptable for ASIL-D determinism (undefined task state transition, no bounded recovery path).
- **REQUIRED FIX:**
  1. Introduce a private static helper `static void unblock_sender_rollback(MessageQueue &q, TaskControlBlock &task) noexcept` in `ipc.cpp` that: removes `task` from `q.blocked_senders_head/tail` intrusive list (mirror the unlink logic already in `wake_sender`, but for an arbitrary node, not just head), sets `task.blocked_on_queue = nullptr; task.blocked_next = nullptr;`, restores `task.state = TaskState::RUNNING;` (or whatever pre-block state constant is used elsewhere in the codebase), and calls `Scheduler::set_task_ready(task)` is **not** appropriate here since the task never actually left the CPU — instead call the scheduler's existing "re-enqueue on ready queue" primitive (`Scheduler::enqueue_ready` / equivalent used internally by `set_task_ready`) to restore ready-queue membership without altering `remaining_ticks`/priority state.
  2. In `IPC::send()`, replace the bare `if (arch::interrupts_enabled())` gate with an explicit branch: if interrupts are **not** enabled, call the new rollback helper immediately after `Scheduler::reschedule();` and **before** falling through to the re-lookup/full-queue-check code, then `return false;` directly (do not re-check `tcb`).
  3. No heap allocation required — this is pure pointer/state manipulation on the existing intrusive list; zero-allocation constraint trivially satisfied.
  4. Add a `static_assert`/`ENSURE` at function entry documenting the precondition: this rollback path must only be reached when `task.blocked_on_queue == &q` (defensive check against double-unlink).

---

- [ ] **VULN-IPC-02**
- **FILE/FUNCTION:** `src/kernel/ipc/ipc.cpp`, `IPC::block_sender()` and `IPC::wake_sender()` (mutating `MessageQueue::blocked_senders_head`/`blocked_senders_tail`/`blocked_next`), cross-referenced against `IPC::send()`'s unguarded `tcb->msg_queue.is_full()` check.
- **ROOT CAUSE:** `MessageQueue::push()`/`pop()` (in the same file) correctly take `SpinLockGuard<sync::SpinLock> guard(lock_)` before touching queue state. `block_sender()` and `wake_sender()` mutate the **same object's** intrusive `blocked_senders_head/tail/blocked_next` fields with **zero locking**, and `IPC::send()` reads `tcb->msg_queue.is_full()` outside any lock before deciding to call `block_sender()`. On an SMP configuration this is a classic TOCTOU + unguarded intrusive-list race: two senders concurrently observing `is_full()==true` can corrupt `blocked_senders_head` (lost update on `*pp = &task`), producing a permanently orphaned blocked sender (silent deadlock — task never rescheduled) or a corrupted linked list (wild pointer dereference on next `wake_sender()`). This is a data race per the C++ memory model and a direct liveness/safety hazard prohibited under ASIL-D freedom-from-interference requirements for shared kernel state.
- **REQUIRED FIX:**
  1. Add the existing `MessageQueue::lock_` (already declared, used by `push`/`pop`) as the sole guard for **all** mutations of `blocked_senders_head`, `blocked_senders_tail`, and any `TaskControlBlock::blocked_next`/`blocked_on_queue` field reachable from a given `MessageQueue`.
  2. In `IPC::block_sender(MessageQueue &q, TaskControlBlock &task)`: acquire `SpinLockGuard<sync::SpinLock> guard(q.lock_);` at function entry, covering the full list-insert and priority-inheritance boost logic, before returning `true`. NOTE: `task.state = TaskState::BLOCKED;` and `Scheduler::dequeue_ready(task);` must remain **outside** or carefully ordered relative to this lock per the existing WEDGE-invariant contract documented above `block_sender` — do not acquire `q.lock_` while holding `scheduler_lock_`, and do not call `dequeue_ready()` (which takes `scheduler_lock_`) while holding `q.lock_` if `scheduler_lock_` is ever taken with a queue lock held elsewhere (lock-ordering must be: `dequeue_ready()` first, unlocked; then acquire `q.lock_` for the list-splice only).
  3. In `IPC::wake_sender(MessageQueue &q, TaskControlBlock &receiver)`: acquire the same `SpinLockGuard<sync::SpinLock> guard(q.lock_);` for the entire head-pop + priority-restore sequence.
  4. In `IPC::send()`, change `if (tcb->msg_queue.is_full())` to call a new `bool MessageQueue::is_full_locked()` that internally takes `lock_` for the read (or reuse `push()`'s existing internal check by refactoring `is_full()` to acquire `lock_` — confirm no re-entrant deadlock since `push()` already takes the same lock separately from this check; the check and the eventual `push()` call are two separate critical sections, which is acceptable since `block_sender()` handles the race window explicitly by re-validating queue state after wake).
  5. Zero dynamic allocation: this fix is lock-guard scoping only, no new storage.

---

- [ ] **VULN-IPC-03**
- **FILE/FUNCTION:** `src/kernel/ipc/ipc.cpp`, `IPC::send_sync()`:
  ```cpp
  cur->reply_wait = true;
  cur->state = TaskState::BLOCKED;
  was_blocked = true;
  Scheduler::reschedule();
  ```
- **ROOT CAUSE:** The same file's `IPC::block_sender()` carries an extensive, explicit contract (documented directly above it) mandating that **every** transition to `TaskState::BLOCKED` must be paired with `Scheduler::dequeue_ready(task)` to preserve the scheduler's WEDGE invariant ("a BLOCKED task must NEVER have `in_ready_queue_ == true`"). `send_sync()` sets `cur->state = TaskState::BLOCKED` but **never calls `Scheduler::dequeue_ready(*cur)`** anywhere in its wait loop. This is a self-contradicting defect within the same translation unit: the WEDGE detector (per the referenced `scheduler.cpp:838` comment) will find `cur` BLOCKED while still `in_ready_queue_ == true`, which per the documented contract triggers the `[WEDGE]` diagnostic and can force a hard halt on an orphaned READY/RUNNING task — a direct availability/safety fault in a certified RTOS path.
- **REQUIRED FIX:**
  1. In `IPC::send_sync()`, immediately before `cur->state = TaskState::BLOCKED;`, insert `kernel::Scheduler::dequeue_ready(*cur);` — matching the exact ordering pattern used in `IPC::block_sender()` (`state = BLOCKED;` then `dequeue_ready()`, per that function's documented "strict order" comment). Apply identically to both the `while (cur->msg_queue.is_empty())` loop body's blocking transition.
  2. Confirm `Scheduler::dequeue_ready()` is idempotent/safe if called on a task not currently in the ready queue (it must be, per its use in `block_sender`); no additional guard needed beyond what `block_sender` already relies on.
  3. No heap allocation involved; this is a single function-call insertion, freestanding-C++20 compliant, zero-cost.

---

- [ ] **VULN-SYNC-01**
- **FILE/FUNCTION:** `src/kernel/sync/mutex.cpp`, `Mutex::lock()` (the **non**-`_err` overload):
  ```cpp
  for (; _pcp_retry < MAX_WAITERS + 1; ++_pcp_retry) { ... }
  lock_.unlock();
  // function returns void here — lock was NOT acquired
  ```
- **ROOT CAUSE:** If the bounded PCP retry loop (`_pcp_retry < MAX_WAITERS + 1`) is exhausted without the calling task ever observing `owner_ == task` (a pathological but structurally reachable exit), `Mutex::lock()` returns to the caller having released `lock_` **without having acquired mutex ownership**, and with **no error signal** — unlike the sibling `lock_err()` overload, which correctly returns `SYNC_ERR_INTERRUPTED` in the equivalent situation. Any caller of the non-`_err` `lock()` API proceeds under the false assumption that it holds exclusive access, leading to unsynchronized concurrent access to whatever data the mutex protects — a silent data-race/corruption vector that is unacceptable for ASIL-D (violates freedom-from-interference guarantees; failure is neither detected nor propagated).
- **REQUIRED FIX:**
  1. Change the `void Mutex::lock()` signature is fixed by the public API surface (`mutex.hpp`) — do **not** break the ABI. Instead, at the point after the retry loop exhausts (the `lock_.unlock();` immediately preceding the implicit `return;` at function end), insert a hard kernel panic/fault, e.g. `panic("Mutex::lock() exhausted PCP retry budget without acquiring ownership — ASIL-D contract violation");` using the same `panic()`/`kernel::Logger::fatal` mechanism already used in `buffer_pool.cpp`'s `bp_check_guard()`. Rationale: a `void`-returning `lock()` has no channel to report failure, so on ASIL-D this must fail *loud and immediately* rather than silently returning unlocked — never allow silent unsynchronized-access continuation.
  2. Alternatively (preferred if API change is permitted upstream by the fixing agent's scope): mark the non-`_err` `lock()` as `[[deprecated("use lock_err() — lock() cannot report PCP retry exhaustion")]]` and route all new call sites to `lock_err()`. Pick ONE of these two remedies — do not implement both.
  3. Zero allocation; single branch + panic call insertion.

---

- [ ] **VULN-SYNC-02**
- **FILE/FUNCTION:** `src/kernel/ipc/ipc.cpp`, `MessageQueue::pop()` — the compaction loop:
  ```cpp
  size_t pos = best_idx;
  while (true) {
      size_t next = (pos + 1) % IPC_MAX_QUEUE_MSG;
      if (next == tail) break;
      msgs[pos] = msgs[next];
      pos = next;
  }
  ```
- **ROOT CAUSE:** This loop is executed while `SpinLockGuard<sync::SpinLock> guard(lock_)` is held for the entire `pop()` call. While the loop is logically bounded by `IPC_MAX_QUEUE_MSG` under correct `head`/`tail` invariants, there is **no explicit, independently-verifiable iteration bound** (`ENSURE`-style guard) — inconsistent with this codebase's own established defensive-coding convention (see `buffer_pool.cpp`'s `unmap_all()` `loop_guard` counter and `bp_check_guard()` red-zone sentinel). Should `head`/`tail`/`count` bookkeeping ever be corrupted by an unrelated defect (e.g. a stray OOB write elsewhere in the kernel's flat address space), this loop has no independent termination check and would run inside a **held spinlock**, starving every other core waiting on `lock_` — a WCET/lock-fairness hazard. This is a defensive-hardening gap, not a proven live bug, but is required to meet this codebase's own established ASIL-D defensive standard.
- **REQUIRED FIX:**
  1. Replace `while (true)` with a bounded `for (size_t iter = 0; iter < IPC_MAX_QUEUE_MSG; ++iter)` loop.
  2. Inside the loop, retain the existing `if (next == tail) break;` early-exit.
  3. After the loop, add `ENSURE(iter < IPC_MAX_QUEUE_MSG);`-style guard (i.e., if the loop runs the full `IPC_MAX_QUEUE_MSG` iterations without hitting `next == tail`, this indicates queue-state corruption) — panic via the existing `assert.hpp` `ENSURE` macro, consistent with the rest of `buffer_pool.cpp`/`ipc.cpp`.
  4. No dynamic allocation; purely a loop-bound/guard change, freestanding C++20 compliant.

---

- [ ] **VULN-SYNC-03**
- **FILE/FUNCTION:** `src/kernel/sync/mutex.cpp` (`Mutex::add_waiter(TaskControlBlock &task)`), `src/kernel/sync/queue.cpp` (`Queue::add_send_waiter(TaskControlBlock *task)`, `Queue::add_recv_waiter(TaskControlBlock *task)`), `src/kernel/sync/semaphore.cpp` (`Semaphore::add_waiter(TaskControlBlock *task)`), `src/kernel/sync/eventgroup.cpp` (`EventGroup::EventWaiter::task` raw pointer member).
- **ROOT CAUSE:** These four synchronization primitives store `TaskControlBlock` handles in long-lived waiter arrays (`waiters_[MAX_WAITERS]`) that persist across a blocking `Scheduler::reschedule()` call, yet: (a) the parameter type is **inconsistent** across the module — `Mutex::add_waiter` takes a reference while `Queue`/`Semaphore` take raw pointers, indicating no single reviewed convention was applied; and (b) **none** of the four primitives validate, at wake time, that the stored `TaskControlBlock*` still refers to the *same logical task* that was originally enqueued (only a `state != TaskState::TERMINATED` liveness check is performed — e.g. `Mutex::wake_one()`, `Semaphore::wake_one()`, `Queue::wake_send_one()/wake_recv_one()`). If the TCB slot is a fixed-pool entry that can be recycled for a new task after termination (the same architectural pattern the codebase explicitly defends against for buffer handles via `BufferPool::Entry::generation` + `BufferPool::validate()`), a stale pointer held in any of these waiter arrays could reference a *reused* TCB, causing an incorrect task to be spuriously readied/priority-boosted — a use-after-reuse safety defect with no detection mechanism, unlike the buffer pool's proven generation-cookie pattern.
- **REQUIRED FIX:**
  1. **API consistency (mechanical, low-risk):** Change `Queue::add_send_waiter`, `Queue::add_recv_waiter`, and `Semaphore::add_waiter` to accept `TaskControlBlock &task` by reference, matching `Mutex::add_waiter`. Update all call sites (`Queue::send`/`send_err`/`receive`/`receive_err`, `Semaphore::wait`/`wait_err`) accordingly — pass `*task` instead of `task` where `task` is already a validated non-null pointer from `Scheduler::current_task()`.
  2. **Stale-reference detection (structural):** Add a `uint32_t generation` field to `TaskControlBlock` (if not already present — verify against `task.hpp`, not provided in this review scope; if absent, add it) that increments every time a TCB pool slot is recycled for a new task. In each of `Mutex::waiters_`, `Semaphore::waiters_`, `Queue::send_waiters_/recv_waiters_`, and `EventGroup::EventWaiter`, additionally store the `generation` value captured at insertion time. At every wake-time dereference (`wake_one()`, `wake_send_one()`, `wake_recv_one()`, `wake_matching()`), compare the stored generation against the live `task->generation` before calling `Scheduler::set_task_ready()`; on mismatch, treat as a stale entry (drop it, do not ready it, do not dereference further).
  3. This is a static, fixed-size field addition to an already-allocated struct — zero dynamic allocation. All arrays remain fixed-size (`MAX_WAITERS` bound unchanged).
  4. Implement as freestanding C++20: no exceptions, no RTTI, no heap; use `static_assert(std::is_trivially_copyable_v<TaskControlBlock*>)`-style compile-time checks only if consistent with existing codebase conventions (check `assert.hpp` for precedent before adding new macros).
