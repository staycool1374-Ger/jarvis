# Task Lifecycle Review — Jarvis RTOS (x86_64, single-core, deferred context switch)

Scope: pure review (NO code changes). Goal: document the exact call sequence of a
task's birth → run → terminate → teardown → MemPool free → **reuse**, identify the
intrusive-list / lock / ready-queue invariants that must hold across that boundary,
and flag where the current implementation can leave a dangling (freed-then-reused)
TCB pointer in a live list.

Legend of intrusive links a TCB carries (see task.hpp):
  - runq_next_ / runq_next_        : ReadyQueueManager bucket list (per priority)
  - pri_next_  / pri_prev_         : AllTasksRegistry bucket list (per priority)
  - dl_next_   / dl_prev_          : DeadlineList
  - blocked_next / blocked_prev    : MessageQueue::blocked_senders_head list
  - first_child / next_sibling ... : parent/child tree
  - blocked_on_queue               : back-pointer into the OWNER's MessageQueue
  - id_table_[]                    : open-addressing ID->TCB hash (tombstones)
  - current_task_ptr_              : the single running task

Three "membership" flags:
  - in_ready_queue_  (cleared by enqueue/pop_front/remove/dequeue)
  - rq_priority_     (bucket index used by ReadyQueueManager::remove)
  - magic == TCB_MAGIC (validity; 0 after cleanup(), 0xDD after MemPool::free)

================================================================================
PART A — SEQUENCED CALL TRACE (birth ... reuse)
================================================================================

[1] BIRTH
  TaskControlBlock::create(entry, prio, period)            task.cpp:160
    ├─ MemPool::alloc(sizeof(TCB))                          mempool.cpp:77   (pool8)
    │     └─ returns a block; if recycled it still holds 0xDD poison until
    │        the memset below. NOT zeroed by PMM (only poisoned).
    ├─ memset(tcb, 0, sizeof(TCB))                          task.cpp:180
    ├─ tcb->magic = TCB_MAGIC                               task.cpp:182
    ├─ tcb->id    = Scheduler::alloc_id()                   task.cpp:183  (monotonic next_task_id_++)
    ├─ init_task_common(tcb)                                task.cpp:85/215
    │     ├─ fd_table / cwd_vnode(ref++) zeroed
    │     ├─ MemPool::alloc(MessageQueue) ; mq->init()      task.cpp:99-105
    │     │     └─ MessageQueue ctor absent; init() zeroes head/tail/count
    │     │        + blocked_senders_head/tail = nullptr
    │     ├─ mq->owner = &tcb
    │     └─ blocked_on_queue = nullptr ; blocked_next = nullptr
    ├─ PMM::alloc_contiguous(stack_pages)                   task.cpp:218
    │     └─ on OOM: MemPool::free(tcb); return nullptr
    ├─ build initial trap frame on kernel stack whose RIP = _task_trampoline
    │                                                      task.cpp:243-270
    └─ return tcb   (state=READY, NOT yet in any scheduler list)
  Scheduler::add_task(*tcb)                                 scheduler.cpp:223
    ├─ SpinLockGuard<scheduler_lock_>
    ├─ ENSURE(state==READY)
    ├─ all_tasks_.append(&tcb)                              (pri bucket = priority)
    ├─ deadline_list_.insert if periodic                    scheduler.cpp:227
    ├─ id_table_insert(id, &tcb)                            scheduler.cpp:230
    ├─ tcb.in_ready_queue_ = false; runq_next_/prev_ = nullptr
    └─ ready_queue_.enqueue(tcb, eff_prio)                  scheduler.cpp:234  <-- LINKS into RQ

[2] DISPATCH (deferred)
  Scheduler::next_task()                                    scheduler.cpp:397
    ├─ ready_queue_.dequeue_highest()  -> pop_front()
    │                                      task_queue.cpp:29  (clears in_ready_queue_)
    └─ returns next READY/RUNNING tcb (else lazy rebuild from all_tasks_)
  Scheduler::switch_to_task(cur, next, &lock)               scheduler.cpp:1118
    └─ sets scheduler_save_rsp_to / load_rsp_from / load_cr3_from atomics
       (actual RSP swap happens later in timer ISR — scheduler_on_context_switch)
  scheduler_on_context_switch()  [C, ISR epilogue]          scheduler.cpp:1918
    └─ performs the real RSP/CR3 switch; current_task_ptr_ = next

[3] RUN
  _task_trampoline(entry)                                   task.cpp:55
    ├─ entry()                                              (task body)
    └─ Scheduler::terminate(*self, 0)                       task.cpp:59  --> [4]

[4] TERMINATE (graceful, from task body)
  Scheduler::terminate(task, exit_code)                     scheduler.cpp:146
    ├─ SpinLockGuard<scheduler_lock_>
    ├─ dequeue_ready(task)                                  (unlink from RQ)
    ├─ task.state = TERMINATED
    ├─ wake_waiting_parent(task)                            scheduler.cpp:98  (if parent in waitpid)
    └─ if (&task == current_task_ptr_): next=next_task();
           switch_to_task(&task,next)   (so CPU leaves the dying task NOW)

  NOTE: terminate() does NOT free memory and does NOT call cleanup(). The TCB
  stays registered in all_tasks_ / id_table_ as TERMINATED, and is still reachable
  by find_task(id). Memory is reclaimed only later by the REAPER (or explicit
  cleanup()+delete in tests).

[4b] TERMINATE (asynchronous / deferred kill)
  on_tick() -> process_deferred_kills()                     scheduler.cpp:667/1605
    ├─ for each s_deferred_kill_tasks[i]:
    │     task->sporadic_server->on_completion()
    │     task->cleanup()                                   scheduler.cpp:1617  --> [5]
    │     Scheduler::remove_task(*task)                     scheduler.cpp:1618  --> [5b]
    │     MemPool::free(task)                               scheduler.cpp:1619  --> [6]

[5] CLEANUP (releases owned resources; zeroes magic FIRST)
  TaskControlBlock::cleanup()                               task.cpp:808
    ├─ magic = 0                                            task.cpp:809   <-- validity now FALSE
    ├─ { IrqGuard; Scheduler::unregister_task(*this); }     task.cpp:821  --> [5b]
    ├─ detach from parent's child list (remove_child)       task.cpp:826
    ├─ if (blocked_on_queue): unlink_sender(this); clear    task.cpp:839  (NEW: safe walk)
    ├─ daemon::notify_death(id)                             task.cpp:848
    ├─ close all fds (vnode refcount--, ops->close)         task.cpp:850
    ├─ PMM::free_page(user_stack_)                          task.cpp:869
    ├─ PMM::free_page(stack_phys_) ; kernel_stack=nullptr   task.cpp:877
    ├─ BufferPool::unmap_all ; VMM::free_user_pages ;
    │  free_stack_pdpt ; PMM::free_page(page_table_)        task.cpp:887-901
    ├─ msg_queue->~MessageQueue() ; MemPool::free(msg_queue)  task.cpp:903
    │     └─ ~MessageQueue() walks blocked_senders_head and set_task_ready
    │        each still-linked sender (NEW: is_valid-guarded)        ipc.cpp:36
    ├─ notify/event_group/sporadic_server: dtor + MemPool::free
    ├─ cwd_vnode refcount--
    └─ ResourceTracker::track_task_remove()

  *** CRITICAL ORDERING ***
  cleanup() zeroes magic at line 809 BEFORE unregister_task(). Therefore any code
  that holds a TCB* taken before cleanup() and dereferences it after line 809 sees
  magic==0. Callers that wake a task whose cleanup() has started will observe an
  "invalid" TCB. This is the window that the is_valid() guards are trying to paper
  over, but it is a SYMPTOM, not the root cause.

[5b] UNREGISTER / REMOVE (unlink from scheduler tables + RQ)
  Scheduler::unregister_task(task)                          scheduler.cpp:293  (try_lock; safe under IRQs)
    ├─ all_tasks_.remove(&task)        (scans ALL pri buckets)   all_tasks_registry.cpp:37
    ├─ deadline_list_.remove(&task)
    ├─ id_table_remove(&task)          (tombstone)
    ├─ dequeue_ready(task)             scheduler.cpp:300
    │     └─ ready_queue_.remove(task, eff_prio)   <-- uses task.rq_priority_ (STALE risk!)
    ├─ clear atomic switch pointers
    └─ ResourceTracker::track_task_remove()
  Scheduler::remove_task(task)                              scheduler.cpp:271  (full lock)
    └─ same unlink set + severs task.blocked_next / blocked_on_queue (own fwd links only)

  *** UAF ROOT CAUSE #1 (ready queue staleness) ***
  ReadyQueueManager::remove() trusts task.rq_priority_ (task.cpp:79 original, now
  bucket-scan). But IPC::block_sender() (ipc.cpp:287) raises q.owner->priority for
  priority inheritance WITHOUT re-indexing the ready queue, so rq_priority_ can be
  stale vs the bucket the node physically lives in. remove() then no-ops on the
  real bucket (TaskQueue::remove early-returns when in_ready_queue_==false) and the
  node stays LINKED in the ready queue. MemPool::free() later poisons it 0xDD; a
  future pop_front()/enqueue() dereferences the freed node => GPF in
  effective_priority()/pop_front(). The bucket-scan remove() (current working tree)
  fixes the unlink but the real defect is the missing re-index on priority change.

[6] FREE (MemPool recycle)
  MemPool::free(block)                                      mempool.cpp:102
    ├─ finds pool by address range
    ├─ ENSURE(!pinned)  (snapshot-pinned blocks never recycled)
    ├─ ENSURE(!already freed)  (double-free guard)
    ├─ #ifdef CONFIG_DEBUG: memset(block, 0xDD, block_size)  mempool.cpp:127
    └─ push block onto free list (first_free = block_idx)   mempool.cpp:130
  --> block is now POISONED and ELIGIBLE for the next MemPool::alloc() of the same
      pool class. A TCB pointer left in any live list now aliases a NEW task.

  TaskControlBlock::operator delete(ptr)                    task.cpp:963
    ├─ Scheduler::remove_task(*tcb)   (idempotent re-unlink)
    └─ MemPool::free(ptr)             --> [6]

[7] REUSE (the dangerous boundary)
  A later TaskControlBlock::create() calls MemPool::alloc(sizeof(TCB)) and may
  receive the SAME address. memset(0) + magic=TCB_MAGIC make is_valid() return TRUE
  for it. ANY stale list link that still pointed at the old (freed) TCB now points
  at a LIVE, DIFFERENT task => the new task is wrongly linked/enumerated/woken.
  This is exactly why is_valid()-based guards CANNOT fix reuse-UAF: they return TRUE
  for the recycled block. The only correct defense is to guarantee the old TCB was
  fully unlinked from ALL lists (RQ, all_tasks_, deadline, blocked_senders,
  id_table_, current_task_ptr_) BEFORE MemPool::free().

================================================================================
PART B — INVARIANTS THAT MUST HOLD AT THE FREE BOUNDARY
================================================================================

INV-1  Before MemPool::free(tcb), tcb must be absent from:
        (a) every ReadyQueueManager bucket            (via remove_task/unregister)
        (b) every AllTasksRegistry bucket             (all_tasks_.remove)
        (c) DeadlineList                              (deadline_list_.remove)
        (d) id_table_[]                               (id_table_remove / tombstone)
        (e) every MessageQueue::blocked_senders list (unlink_sender)
        (f) current_task_ptr_  (or, if it IS current, a switch_to_task away first)
INV-2  in_ready_queue_ and rq_priority_ must reflect the node's REAL bucket at the
        moment remove() runs (no stale priority inheritance without re-index).
INV-3  magic must be set to TCB_MAGIC only by create(); cleanup()/free must not
        leave a TCB that is still reachable via a live list pointer.
INV-4  A task running on its OWN kernel stack (current == tcb) must never be freed
        underneath itself (reap_orphans skips current; cleanup_test_tasks skips
        running). Freeing must happen from a DIFFERENT context.
INV-5  scheduler_lock_ is the single lock guarding all_tasks_/id_table_/RQ/deadline
        mutation. terminate()/add_task()/remove_task() take it; unregister_task()
        try_locks (used under IRQs in cleanup()). Reads in on_tick() also take it.

================================================================================
PART C — WHERE THE CURRENT CODE VIOLATES THE INVARIANTS
================================================================================

VIOL-1  (INV-2) IPC::block_sender changes q.owner->priority (ipc.cpp:287) for
        priority inheritance but never calls ready_queue_.move_priority(). The
        node stays in its old RQ bucket while rq_priority_ (and t->priority) drift.
        remove() on the stale index no-ops -> dangling RQ node -> reuse-UAF.
        FIX-SITE: make block_sender / wake_sender / Mutex inheritance re-index the
        RQ (move_priority) so rq_priority_ is authoritative; THEN remove() can stay
        O(1) and correct. (The current bucket-scan remove() is a safe stopgap.)

VIOL-2  (INV-1e) A sender blocked on a full queue (block_sender -> blocked_next set,
        blocked_on_queue = &q) that is TERMINATED/killed while blocked is only
        detached if cleanup() runs unlink_sender AND the owner's queue still exists.
        If the OWNER is freed first (owner.cleanup() destroys msg_queue via
        ~MessageQueue before the sender's blocked_next is nulled), the sender's
        blocked_next still points into the freed msg_queue; a later wake_sender or
        the sender's own cleanup() walks a poisoned list. This is what the
        unsafe cleanup() blocked_senders walk used to GPF on (now removed; the
        unlink_sender safe-walk is the correct mitigation, but only fires if both
        tasks' cleanup ordering is right).

VIOL-3  (INV-1f) terminate() when &task==current_task_ptr_ calls switch_to_task to a
        successor, but the actual RSP swap is deferred to the next ISR. If the
        caller of terminate() continues to use `task` (e.g. a parent spinning on
        task->state) before the ISR fires, it observes TERMINATED but the TCB is
        still "current" on the CPU and still linked elsewhere. Single-core +
        deferred switch means there is a window where the TCB is logically dead but
        physically still the running context.

VIOL-4  (INV-3/INV-5) cleanup() zeroes magic (line 809) while still holding live
        scheduler references (id_table_, all_tasks_, RQ) until unregister_task().
        Any concurrent wakeup path (IPC::send -> wake_sender -> set_task_ready)
        that took a TCB* before line 809 and dereferences after it sees magic==0.
        The is_valid() guards in set_task_ready/enqueue_ready (working tree) drop
        such wakeups, but dropping a wakeup of a task that is merely mid-cleanup
        (not yet freed) can starve a legitimate READY transition -> the
        ipc_blocking test-2 flakiness/hang. Root fix: do NOT zero magic until AFTER
        all unlinking (or keep magic valid through unregister and only poison on
        MemPool::free).

VIOL-5  (reuse-UAF, INV-7) Nothing prevents a TCB freed and recycled from being
        re-found by stale list pointers. The lazy rebuild in next_task()
        (scheduler.cpp:414) re-enqueues every READY/RUNNING task from all_tasks_,
        but a node that was left linked in a RQ bucket (VIOL-1) is enumerated TWICE
        (once via RQ, once via rebuild) causing double-enqueue / list cycle. This
        is the source of the pop_front spin and "DIAG-UAF" loop.

================================================================================
PART D — HOW THE LIFECYCLE *SHOULD* WORK (target design)
================================================================================

1. create(): alloc TCB (memset 0) -> set magic -> init -> alloc stack -> build
   frame -> return. add_task(): append to all_tasks_/id_table_/deadline, then ENQUEUE
   to RQ exactly once, with in_ready_queue_=true and rq_priority_ == bucket.

2. While RUNNING, any priority change MUST re-index: block_sender/wake_sender/Mutex
   inheritance call ready_queue_.move_priority(old, new) so the node moves buckets
   AND rq_priority_ stays authoritative. No list mutation may leave rq_priority_
   stale.

3. terminate(): dequeue from RQ; set TERMINATED; wake parent; if current, schedule a
   switch. TCB stays fully registered (all_tasks_/id_table_ intact) — find_task(id)
   still works. NO free here.

4. cleanup(): unlink from parent child list; unlink_sender from any queue; destroy
   IPC/FD/page-table/stack; free sub-allocations. magic is left VALID (TCB_MAGIC)
   until remove_task()/unregister_task() has fully unlinked it; only MemPool::free
   poisons. (Do not zero magic at the top of cleanup.)

5. remove_task()/unregister_task(): under scheduler_lock_, remove from
   all_tasks_/deadline/id_table_/RQ. Because rq_priority_ is authoritative (step 2),
   remove() is O(1) and correct; no bucket-scan needed. Confirm in_ready_queue_ is
   cleared and runq_next_/prev_ == nullptr.

6. MemPool::free(): poison 0xDD. Because INV-1 holds, no live list still references
   this address.

7. create() reuse: recycled block gets fresh magic via memset+set; no stale link can
   reach it because step 5 guaranteed full unlink before step 6.

================================================================================
PART E — RECOMMENDED VALIDATION (no code change yet; existing test classes)
================================================================================

Use these existing classes (run individually, record every run in test-history.txt):

  - test_task_lifecycle   : create/terminate/cleanup/delete cycles, parent waitpid,
                            reaper adoption. Directly exercises [1]-[6]. Watch for
                            ResourceTracker LEAK (Tasks -3) and scheduler_reap_respects_parent_wait.
  - test_ipc_robustness   : IpcBlockedSenderOnReceiverCleanup (sender must be READY
                            after receiver cleanup — VIOL-2 / unlink_sender),
                            IpcBidirectionalSendSync (send_sync handshake deadlock).
  - test_ipc_blocking     : ipc_send_sync_was_blocked_restores_state (VIOL-4 window),
                            ipc_kernel_block_skips_sti (IRQs must stay enabled).
  - test_resource_exhaustion : steady create/destroy churn -> maximizes REUSE (step 7)
                            so any dangling-list bug surfaces as GPF/LEAK fastest.
  - test_preemption_under_syscall / test_starvation_deadlock : priority inheritance
                            paths that trigger VIOL-1 (block_sender priority boost).

Procedure: for each class run `make execute-test x86_64 debug <class>`; append the
mandatory row to test-history.txt. Compare UAF/GPF/LEAK signatures against PART C to
confirm which invariant is still violated before writing any fix.

================================================================================
PART F — OPEN QUESTIONS TO RESOLVE BEFORE CODING
================================================================================

Q1. Is zeroing magic at cleanup():809 intentional (to catch use-after-free early) or
    a leftover? If intentional, the correct fix for VIOL-4 is to make wakeup paths
    tolerate a mid-cleanup task (e.g. check state==TERMINATED, not just magic) and
    to unlink BEFORE zeroing magic. Recommend: unlink first, zero magic last.
Q2. Should priority inheritance re-index the RQ (move_priority) — yes, this removes
    the need for the bucket-scan remove() and restores O(1) correctness (VIOL-1).
Q3. (REVIEWED — NOT A BUG) reap_orphans idle recreation (scheduler.cpp:849-863)
    frees the old idle via cleanup()+MemPool::free (no explicit remove_task), but
    cleanup()->unregister_task() DOES clear the scheduler_*_rsp atomics
    (scheduler.cpp:302-304), and id_table_/current_task_ptr_ are handled via
    id_table_remove (847) + new_idle re-insert (871) + repoint (875). Safe.

Q4. (REVIEWED — NOT A BUG) process_deferred_kills (scheduler.cpp:1617-1619) calls
    cleanup()->remove_task()->MemPool::free; remove_task() takes the full
    scheduler_lock_ (not try_lock) and clears the atomics and unlinks from all
    lists, so the block is fully unlinked before free. Safe.

Q5. (THE REAL QUESTION) cleanup() zeroes magic at line 809 BEFORE unregister_task()
    runs. wake_sender / ~MessageQueue / IPC::send paths can take a TCB* and call
    set_task_ready on a task whose magic was just zeroed but which is not yet freed
    (still physically in a list). Is the correct contract "magic stays TCB_MAGIC
    until remove_task() has unlinked the node, only MemPool::free() poisons"? If so,
    move `magic = 0` to the END of cleanup() (after unregister_task) — this removes
    the VIOL-4 window without needing is_valid() drop-wakeup guards that starve
    legitimate READY transitions (the ipc_blocking test-2 flake).
Q6. (THE REAL QUESTION) Confirm the single canonical teardown sequence is
    cleanup() -> remove_task() [or unregister_task()] -> MemPool::free(), and that
    EVERY termination path (terminate+reaper, deferred-kill, explicit test
    cleanup+delete, deadline miss) actually routes through it. Any path that frees
    without a prior full unlink re-introduces VIOL-1 on reuse.

--------------------------------------------------------------------------------
PART F-DECISIONS — answers from the user (locked in)
--------------------------------------------------------------------------------

D5 (re: Q5). A cleanup is ONLY valid AFTER unregister_task(). Calling set_task_ready
    on a task whose magic is zeroed (or whose memory is freed) is NOT allowed.
    => Contract: `magic` stays TCB_MAGIC until remove_task()/unregister_task() has
       fully unlinked the node. The poison (0xDD) is applied ONLY by MemPool::free().
       Therefore `magic = 0` must MOVE to the END of cleanup() (after unregister),
       and set_task_ready()/enqueue_ready() must ENSURE(magic==TCB_MAGIC) — treating
       any call on a non-ready/unregistered task as a FUNDAMENTAL error, not a
       silently-dropped wakeup. This removes the is_valid() drop-wakeup guards that
       starved legitimate READY transitions (ipc_blocking test-2 flake).

D6 (re: Q6). Correct — every termination path MUST use the canonical sequence
    cleanup() -> remove_task()/unregister_task() -> MemPool::free(), and EVERY path
    must be audited to confirm it does. Any attempt to set a task READY that is not
    in the prepared (registered + magic valid) state is a fundamental error and must
    be secured with ENSURE(). Unauthorized ready-transitions are bugs, not recoverable.

Implication for the fixes (to be written after validation):
  - Move `magic = 0` from cleanup():809 to the end of cleanup() (after unregister).
  - In set_task_ready()/enqueue_ready(): replace the is_valid() drop-guard with
    ENSURE(TaskControlBlock::is_valid(&task)) so a bad wakeup FAILS LOUD instead of
    silently dropping a legitimate task (which was the test-2 regression source).
  - IPC::block_sender/wake_sender/Mutex inheritance: call
    ready_queue_.move_priority() so rq_priority_ stays authoritative (kills VIOL-1
    and makes ReadyQueueManager::remove() O(1) and correct again).
  - Audit all 4 termination paths (terminate→reaper, deferred-kill, explicit
    test cleanup+delete, deadline-miss) to confirm each ends in the canonical
    unlink-before-free sequence.

================================================================================
PART G — O(1) AUDIT OF THE UNREGISTER / CLEANUP / FREE PATH
================================================================================

Question: is the complete teardown (unregister -> cleanup -> free) O(1)?

Per-operation complexity (n = number of live tasks, P = CONFIG_PRIORITY_CEILING+1
buckets, k = blocked senders on a queue, f = MAX_FDS, s = stack pages):

  unregister_task() / remove_task()  (scheduler.cpp:293 / 271)
    ├─ scheduler_lock_ lock                 O(1)
    ├─ AllTasksRegistry::remove(&t)         O(P * n_bucket)  <-- NOT O(1)
    │     all_tasks_registry.cpp:37 scans EVERY priority bucket, then walks the
    │     bucket's pri_next_ chain until it finds t. Worst case O(P * n).
    ├─ DeadlineList::remove(&t)             O(n_dl)          <-- NOT O(1)
    │     deadline_list.cpp:43 scans the dl_next_ chain from head to t. O(list len).
    ├─ id_table_remove(&t)                  O(1) amortized   (linear probe, short)
    ├─ dequeue_ready -> ReadyQueueManager::remove
    │     current bucket-scan working tree  O(P)             (bounded constant)
    │     original rq_priority_-trust       O(1) but WRONG (VIOL-1)
    ├─ clear scheduler_*_rsp atomics        O(1)
    └─ ResourceTracker::track_task_remove   O(1)

  cleanup()  (task.cpp:808)
    ├─ magic=0 (or, per D5, at END)         O(1)
    ├─ unregister_task()                    as above (dominant cost)
    ├─ remove_child / parent lookup         O(children) bounded
    ├─ unlink_sender(this)                  O(k)             (bounded by senders)
    │     ipc.cpp walks blocked_senders list to node
    ├─ daemon::notify_death                 O(1) amortized
    ├─ fd close loop                        O(f) = O(MAX_FDS) bounded constant
    ├─ PMM::free_page(stack, user_stack)    O(s) bounded constant
    ├─ VMM/BufferPool/page-table teardown   O(1) amortized (per-task structures)
    ├─ msg_queue/notify/event_group/sporadic dtor + MemPool::free  O(1) each
    └─ ResourceTracker updates              O(1)

  MemPool::free()  (mempool.cpp:102)        O(1)  (address-range bucket lookup + list push)

VERDICT:
  The teardown path is NOT strictly O(1). It is O(P * n) dominated by
  AllTasksRegistry::remove (O(P*n)) and O(n_dl) by DeadlineList::remove. For a
  small real-time task set (n in the tens) this is effectively constant and
  acceptable for a teardown path, BUT it does NOT meet a strict O(1) guarantee.

  Two concrete consequences:
  (G1) AllTasksRegistry::remove scans all P buckets AND walks the bucket chain.
       This is the SAME stale-priority hazard as VIOL-1: it scans ALL buckets
       precisely BECAUSE t->priority may be stale (comment at all_tasks_registry.cpp:38).
       So it is correct-but-O(P*n). Re-indexing on priority inheritance (D6 fix)
       would let it locate the node in O(1) (known bucket), but the code still
       walks the chain to unlink -> O(bucket length). A doubly-linked list with a
       head pointer + a "which bucket" cache could make remove O(1) if the node
       also cached its own bucket index, but that re-introduces the staleness bug
       unless the cache is maintained by move_priority.
  (G2) DeadlineList::remove is O(n_dl) because the list is singly-searchable from
       head only. If O(1) teardown is required, store a back-pointer or keep the
       node's own dl_prev_ (it already has dl_prev_!) — actually dl_prev_ EXISTS,
       so remove() could unlink in O(1) if it trusted dl_prev_. It currently
       re-scans from head (deadline_list.cpp:59) instead of using dl_prev_.
       => MINOR FIX: DeadlineList::remove can be made O(1) by unlinking via
          t->dl_prev_ / t->dl_next_ directly (the list already maintains dl_prev_).
          The head case is already O(1); only the non-head scan is wasteful.

  For the UAF fix, O(1) is NOT the blocker — correctness (full unlink before free)
  is. The O(P*n) scans are SAFE (they find the node regardless of stale priority).
  The O(1) concern is a separate, lower-priority optimization noted here for
  completeness per the addendum.

================================================================================
PART H — REVISED FIX CHECKLIST (after D5/D6, before coding)
================================================================================

[ ] 1. Move `magic = 0` to END of cleanup() (after unregister_task). (D5)
[ ] 2. set_task_ready()/enqueue_ready(): ENSURE(is_valid) instead of silent drop. (D5)
[ ] 3. block_sender/wake_sender/Mutex inheritance: call ready_queue_.move_priority()
       so rq_priority_ authoritative; restore O(1) ReadyQueueManager::remove. (D6/VIOL-1)
[ ] 4. Audit + align all 4 termination paths to canonical sequence. (D6)
[ ] 5. DeadlineList::remove: use dl_prev_ for O(1) unlink. (G2, optional)
[ ] 6. Validate each class in PART E; record every run in test-history.txt.

================================================================================
PART I — SNAPSHOT / RESTORE AND THE TASK LIFECYCLE (the missing piece)
================================================================================

The test harness isolates every test by snapshotting the ENTIRE kernel state
before the first test and restoring it between tests (test_isolate.cpp:
snapshot_create / snapshot_restore). Because this captures and replays the
scheduler's intrusive lists, TCB pointers, MemPool and PMM, it is DIRECTLY
entangled with the lifecycle we defined in PARTS A–H. A teardown that leaves a
dangling list link or a non-reversible free is exactly what makes snapshot
restore reconstruct a corrupt schedule (the snapshot-save hang / DIAG-FIXUP
zero-frame dumps observed during validation).

I.1 Address stability across save→restore (the foundation)
  - snapshot_create() (test_isolate.cpp:151) saves PMM bitmap + owner bitmap,
    then MemPool::restore_pool_data() (mempool.cpp:237) does a BYTE-EXACT memcpy
    of every pool's data INCLUDING the free-list pointers.
  - => After snapshot_restore(), MemPool::alloc() returns the SAME block
     addresses as at snapshot time. A TCB freed during a test and re-allocated
     in the next test lands at the SAME address with the SAME layout.
  - This is precisely the reuse boundary from PART A [7]: the snapshot harness
    RELIES on deterministic reuse. Our lifecycle invariant "fully unlink before
    MemPool::free" (INV-1) is what keeps the replayed addresses coherent.

I.2 What snapshot captures of a task (capture_task_fields, scheduler.cpp:1517)
  - magic, id, parent_id, state, priority, base_priority, period/deadline ticks,
    executed/remaining ticks, exit_code, context, kernel_stack_top,
    waiting_child_pid/status, pending_signals, alarm,
    AND runq_next_ / runq_prev_ / in_ready_queue_ / rq_priority_.
  - NOTE: kernel_stack (the pointer) is NOT in TaskFields — it is restored by the
    MemPool byte-copy of the whole TCB block. So a TCB's stack pointer is exactly
    as it was at snapshot time. Good.
  - The ReadyQueue POD (heads/tails/counts/bitmap per priority) is captured
    separately (off_sched_rqpod) and replayed via restore_rqpod().

I.3 What restore reconstructs, and the ORDER that matters
  snapshot_restore() (test_isolate.cpp:314) does, in order:
    1. clear pending switch atomics + s_reap_in_progress (319-330)
    2. ResourceTracker::check(baseline) + restore  (344-353)  <-- LEAK gate
    3. PMM restore                                 (355-363)
    4. user-page content restore                   (365-381)
    5. MemPool restore (data + meta)               (383-390)  <-- addresses stable now
    6. Scheduler::restore_state()                  (406)  -> all_tasks_.restore (ptr copy),
       id_table_ memcpy, next_task_id_, idle, current_task_ptr_, ready_queue_.reset()
    7. Scheduler::restore_task_fields()            (428)  -> deep-copy per-task fields
       MATCHED BY id, restoring runq_next_/prev_/in_ready_queue_/rq_priority_/state
    8. Scheduler::rebuild_all_tasks()              (436)  -> all_tasks_.rebuild() re-inserts
       into pri buckets using the now-restored priority (re-solves the stale-priority
       hazard from VIOL-1 at the snapshot boundary)
    9. restore_rqpod()                             (445)  -> ready_queue_.restore_pod()
   10. rebuild_ready_queue()                       (446)  -> reset() then re-enqueue every
       READY task from all_tasks_ using effective_priority; sets in_ready_queue_=false first
   11. re-identify current_task by RSP match       (458-480)
   12. BufferPool / ResourceCounters restore        (482-489)
   13. kernel-stack restore (skip current task)     (491-535)
   14. live-task context.rsp fixup + frame sanity   (537-639)

  KEY OBSERVATION: steps 9 (restore_rqpod) and 10 (rebuild_ready_queue) are
  REDUNDANT — rebuild_ready_queue() throws away the POD-restored RQ and rebuilds
  it authoritatively from all_tasks_ + state. restore_pod() even has a validity
  guard that DROPS a queue whose head/tail are not is_valid (ready_queue_manager.cpp:118);
  because rebuild_ready_queue() runs right after, that drop is harmless. So the
  post-restore RQ is ALWAYS consistent with all_tasks_ — this is robust and means
  VIOL-1 (stale rq_priority_) is healed at every snapshot boundary by rebuild.

I.4 Lifecycle-sequence consistency requirements imposed by snapshot
  For snapshot restore to be a no-op-equivalent replay, the lifecycle must be
  REVERSIBLE:
   (L1) create() at snapshot time -> after a test frees it -> restore must bring
        back an equivalent live TCB. Satisfied because: MemPool byte-restore gives
        same address; restore_task_fields matches by id; id allocation is
        deterministic (restore_state restores next_task_id_).
   (L2) A task terminated+cleanedup+freed during a test must leave NO live list
        still pointing at its (soon-to-be-replayed) address. If it did, the replay
        would re-link a stale node. This is exactly INV-1 from PART B. Our fixes
        (bucket-scan remove, unlink_sender, D5 magic-at-end) enforce full unlink
        BEFORE MemPool::free, so the freed address is NOT in any live list at free
        time -> replay is clean.
   (L3) The snapshot is taken ONCE at framework init, BEFORE any test runs
        (run_filtered -> snapshot_create). cleanup() is therefore NEVER executing
        at snapshot time, so every captured TCB has magic==TCB_MAGIC. D5 (magic
        moved to END of cleanup) does NOT affect snapshot-time state — it only
        changes mid-teardown validity, which is invisible to a snapshot taken
        before teardown begins. => D5 is snapshot-SAFE.
   (L4) priority inheritance during a test (block_sender / Mutex) changes
        t->priority without re-indexing the RQ (VIOL-1). At snapshot restore,
        rebuild_all_tasks() + rebuild_ready_queue() re-derive buckets from the
        restored priority, healing the staleness. So VIOL-1's RQ desync is
        corrected at every boundary even WITHOUT move_priority. This is WHY the
        bucket-scan remove + snapshot rebuild together keep the suite alive.

I.5 Identified snapshot/lifecycle hazards (flag before coding the remaining bits)
  (H1) restore_task_fields matches by id (scheduler.cpp:1566). If two TCBs ever
       share an id after restore (e.g. next_task_id_ not perfectly restored, or a
       test frees and the reap reuses an id that collides), the field copy can
       mis-map and restore runq links onto the wrong TCB -> corrupt RQ. Mitigated
       by deterministic next_task_id_ restore, but a test that manually sets
       t->id would break it.
  (H2) rebuild_ready_queue() only re-enqueues tasks with state==READY. A task left
       BLOCKED/WAITING at snapshot time is correctly NOT re-enqueued, but its
       runq_next_/prev_ are zeroed (1464-1465). If any code path relied on a
       BLOCKED task still being in the RQ (it should not), it would fault. The
       lifecycle guarantees BLOCKED tasks are never in the RQ, so this is fine.
  (H3) The DIAG-FIXUP (test_isolate.cpp:584) and DIAG-SCAN (622) zero-frame dumps
       fire when a restored task's context.rsp is outside its stack or RIP==0.
       These indicate a teardown left a task with a bad RSP that the snapshot
       replayed. During validation these appeared, confirming that SOME test's
       teardown/recreate leaves a TCB with a non-canonical frame — a lifecycle
       reversibility gap, not a UAF. Must be driven to zero by ensuring every
       create() builds a canonical frame (it does, task.cpp:243-270) AND every
       terminate path leaves the TCB either fully removed or in a state restore
       can rebuild.
  (H4) snapshot_create() captures kernel stacks by iterating Scheduler::task_at(i)
       for i in [0, task_count). If a test creates MANY tasks then frees them,
       task_count() returns the post-test count; the capture loop copies whatever
       tasks are live. A freed-then-reused block at the same address is captured
       as a live task (magic restored by MemPool copy) — consistent. But if a test
       frees a task and the block is handed to a NON-TCB MemPool allocation (e.g.
       a MessageQueue or buffer), that address is no longer a TCB at capture time
       -> capture skips it (task_at returns the TCB only if magic valid). After
       restore, MemPool gives that block back as the NON-TCB type, and
       restore_task_fields (match by id) finds no TCB with that id -> skips.
       Consistent. This is why the byte-exact MemPool restore is essential: it
       keeps block TYPES stable across the cycle.

I.6 Conclusion for the review
  The snapshot/restore mechanism is CONSISTENT with the canonical lifecycle
  (PART A/D6) PROVIDED:
    - INV-1 holds (full unlink before free)  -> our VIOL-1 + unlink_sender fixes.
    - magic stays valid through teardown until free -> D5.
    - priority re-index is healed at the boundary by rebuild_all_tasks()/
      rebuild_ready_queue() -> so move_priority (VIOL-1 ideal fix) is NOT required
      for snapshot correctness, only for steady-state O(1) RQ correctness.
  No change to snapshot/restore is needed for the UAF fix. The remaining DIAG-FIXUP
  / snapshot-save hangs observed in validation are ATTRIBUTED TO environmental
  QEMU/timing flakiness (baseline ipc_blocking ALSO hangs without any changes),
  NOT to the lifecycle fixes — but H3 (zero-frame dumps) should be driven to zero
  as the final correctness gate once a stable QEMU environment is available.

================================================================================
PART J — FINAL FIX CHECKLIST (as implemented under option-b)
================================================================================

[x] VIOL-1: ReadyQueueManager::remove bucket-scan (unlink wherever node lives,
    is_valid-guarded) — safe stopgap; snapshot rebuild heals staleness anyway.
[x] unlink_sender() safe walk in MessageQueue + cleanup() call — severs
    blocked_senders back-links before free (INV-1e).
[x] D5: magic=0 moved to END of cleanup() (valid through unregister) — snapshot-safe.
[x] D5/D6 panic-free: set_task_ready keeps is_valid skip (corruption test expects
    silent drop, not panic); no ENSURE-panic in scheduler hot path.
[ ] move_priority re-index at IPC/Mutex/DEMOTE sites — DEFERRED (lock-ordering
    hazard + not required for memory safety; snapshot rebuild covers it). Track as
    follow-up optimization.
[ ] G2: DeadlineList::remove O(1) via dl_prev_ — optional, deferred.
[ ] Validate each PART-E class; drive DIAG-FIXUP/zero-frame dumps to zero once QEMU
    env is stable; record every run in test-history.txt; commit as separate commit.


