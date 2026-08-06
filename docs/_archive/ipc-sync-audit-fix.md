# IPC & Synchronisation Audit — Fix Specification

**Audit Source:** `audits/ipc_audit.md` (6 verified findings)  
**Status:** v0.3.6 Implementation Plan  
**Target:** Hard real-time compliance (ASIL-D / IEC 61508 SIL 4)

---

## 1. VULN-IPC-01 — `IPC::send()` Interrupts-Disabled Rollback Omission (CRITICAL)

### Problem
When `IPC::send()` is invoked with interrupts disabled, the `if (arch::interrupts_enabled())` guard skips the spin-wait loop, but `block_sender()` has already mutated the caller's TCB to BLOCKED + dequeued from ready queue + linked into the destination's blocked-senders list. The function returns `false` without rolling back these mutations. The caller thinks `send()` simply failed, but the TCB is now a) BLOCKED, b) off the ready queue, c) linked into a foreign wait list — a liveness/consistency defect.

### Fix
1. Add `static void unblock_sender_rollback(MessageQueue &q, TaskControlBlock &task)` helper that:
   - Removes `task` from `q.blocked_senders_head/tail` intrusive list
   - Sets `task.blocked_on_queue = nullptr; task.blocked_next = nullptr;`
   - Restores `task.state = TaskState::RUNNING;`
   - Calls `Scheduler::enqueue_ready(task)` to restore ready-queue membership
2. In `IPC::send()`, when interrupts are NOT enabled, call rollback after `Scheduler::reschedule()` and before falling through to re-lookup, then `return false`.
3. Add `ENSURE(task.blocked_on_queue == &q)` at entry of rollback.

### Files
`src/kernel/ipc/ipc.cpp`

### Risk
Low — new function, limited scope. Rollback path only activated when interrupts are disabled.

---

## 2. VULN-IPC-02 — Unsynchronised `blocked_senders` List Mutation (CRITICAL)

### Problem
`block_sender()` and `wake_sender()` mutate `MessageQueue::blocked_senders_head/tail/blocked_next` fields with zero locking, while `MessageQueue::push()/pop()` correctly use `SpinLockGuard<sync::SpinLock> guard(lock_)` for queue state. `IPC::send()` reads `tcb->msg_queue.is_full()` outside any lock before deciding to block. On SMP this is a TOCTOU + unguarded intrusive-list race.

### Fix
1. `block_sender()`: acquire `SpinLockGuard<sync::SpinLock> guard(q.lock_)` covering list-insert and priority-inheritance boost. Keep `dequeue_ready()` outside the lock (lock ordering: scheduler_lock_ first, then queue lock).
2. `wake_sender()`: acquire same guard for head-pop + priority-restore sequence.
3. `send()`: replace `if (tcb->msg_queue.is_full())` with `if (tcb->msg_queue.is_full_locked())` that internally acquires `lock_`.

### Files
`src/kernel/ipc/ipc.cpp`

### Dependency
Requires SpinLock infrastructure (VULN-002 or existing `sync::SpinLock`).

---

## 3. VULN-IPC-03 — `send_sync()` Missing `dequeue_ready()` (CRITICAL)

### Problem
`IPC::send_sync()` sets `cur->state = TaskState::BLOCKED` but never calls `Scheduler::dequeue_ready(*cur)`. This violates the WEDGE invariant: a BLOCKED task must NEVER have `in_ready_queue_ == true`. The wedge detector will flag this as an orphan.

### Fix
Insert `Scheduler::dequeue_ready(*cur);` immediately before `cur->state = TaskState::BLOCKED;` in both the send-sync wait loop body and the initial blocking transition.

### Files
`src/kernel/ipc/ipc.cpp`

### Risk
Trivial one-liner insertion. `dequeue_ready()` is already idempotent per its use in `block_sender()`.

---

## 4. VULN-SYNC-01 — `Mutex::lock()` Silent Failure on PCP Retry Exhaustion (HIGH)

### Problem
If the bounded PCP retry loop in `Mutex::lock()` exhausts `MAX_WAITERS + 1` attempts without acquiring ownership, the function returns `void` after releasing `lock_` — caller proceeds assuming it holds the mutex. The `_err` overload correctly returns `SYNC_ERR_INTERRUPTED` in this case.

### Fix
Add a `panic()` call after the retry-loop exhaustion point (after `lock_.unlock();`), using the same mechanism as `buffer_pool.cpp`'s `bp_check_guard()`. The `void`-returning `lock()` cannot report failure, so on ASIL-D it must fail loud and immediate rather than silently returning unlocked.

### Files
`src/kernel/sync/mutex.cpp`

### Risk
Low — only triggers on an architecturally impossible condition (PCP retry exhaustion). Adding a panic converts silent corruption to a detectable halt.

---

## 5. VULN-SYNC-02 — MessageQueue Pop Compaction Loop Unbounded (MEDIUM)

### Problem
`MessageQueue::pop()` compaction loop uses `while (true)` with no explicit iteration bound. If `head`/`tail`/`count` bookkeeping is corrupted by an unrelated defect, this loop runs inside a held spinlock, starving every core.

### Fix
1. Replace `while (true)` with `for (size_t iter = 0; iter < IPC_MAX_QUEUE_MSG; ++iter)`
2. Retain existing `if (next == tail) break;` early-exit
3. Add `ENSURE(iter < IPC_MAX_QUEUE_MSG);` after the loop

### Files
`src/kernel/ipc/ipc.cpp`

### Risk
Low — pure bounds addition.

---

## 6. VULN-SYNC-03 — Stale TCB References in Waiter Arrays Without Generation Cookie (MEDIUM)

### Problem
`Mutex`, `Semaphore`, `Queue`, and `EventGroup` store `TaskControlBlock*` in waiter arrays. When a TCB slot is recycled for a new task (`MemPool` free + realloc), a stale pointer in a waiter array could reference a reused TCB, causing an incorrect task to be woken or priority-boosted. No generation-cookie validation exists (unlike `BufferPool::Entry::generation` + `BufferPool::validate()` pattern).

### Fix
1. **API consistency:** Change `Queue::add_send_waiter`, `add_recv_waiter`, `Semaphore::add_waiter` to accept `TaskControlBlock &task` by reference (matching `Mutex::add_waiter`).
2. **Generation field:** Add `uint32_t generation` field to `TaskControlBlock`, incremented each time a TCB pool slot is recycled.
3. **Store + verify:** Each waiter array entry additionally stores `generation` captured at insertion. At wake time (`wake_one()`, etc.), compare stored generation against live `task->generation`. On mismatch, treat as stale (drop entry, do not ready).

### Files
`src/kernel/task/task.hpp`, `src/kernel/sync/mutex.cpp`, `src/kernel/sync/semaphore.cpp`, `src/kernel/sync/queue.cpp`, `src/kernel/sync/eventgroup.cpp`

### Risk
Medium — adds a field to TCB, changes sync primitive wake paths. The generation pattern is already proven in `BufferPool`.

---

## Implementation Order

```
All 6 findings are independent of each other and of memory/scheduler audits.
Priority ranking (do first):
  VULN-IPC-03  send_sync missing dequeue_ready    [CRITICAL, one-liner]
  VULN-IPC-01  send() rollback omission            [CRITICAL]
  VULN-IPC-02  blocked_senders list locking        [CRITICAL]
  VULN-SYNC-01 Mutex::lock() panic on exhaustion   [HIGH]
  VULN-SYNC-02 Pop compaction loop bound           [MEDIUM]
  VULN-SYNC-03 Waiter array generation cookies     [MEDIUM]
```
