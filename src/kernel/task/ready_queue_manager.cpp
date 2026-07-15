/// @file ready_queue_manager.cpp
/// @brief ReadyQueueManager implementation: O(1) enqueue/dequeue by priority.

#include <kernel/task/ready_queue_manager.hpp>
#include <kernel/task/task.hpp>

extern "C" void debug_write(const char *s);
extern "C" void debug_write_hex(uint64_t value);

namespace kernel {

void ReadyQueueManager::enqueue(TaskControlBlock &tcb,
                                uint64_t priority) noexcept {
    // Guard against double-enqueue: re-enqueueing an already-queued TCB
    // corrupts the intrusive runq_next_/runq_prev_ links and can deadlock the
    // scheduler (next_task() loops, reschedule() spins holding
    // scheduler_lock_).
    if (tcb.in_ready_queue_) {
        static bool diag_dumped = false;
        if (!diag_dumped) {
            bool phys = false;
            for (auto *n = queues_[priority].head(); n; n = n->runq_next_) {
                if (n == &tcb) { phys = true; break; }
            }
            debug_write("[DIAG-ENQ] REFUSED in_rq=1 physically_in_queue=");
            debug_write_hex(phys ? 1u : 0u);
            debug_write(" id=");
            debug_write_hex(tcb.id);
            debug_write(" prio=");
            debug_write_hex((uint64_t)priority);
            debug_write(" state=");
            debug_write_hex((uint64_t)tcb.state);
            debug_write("\n");
            diag_dumped = true;
        }
        return;
    }
    queues_[priority].push_back(tcb);
    bitmap_.set(priority);
    tcb.rq_priority_ = priority;
    tcb.in_ready_queue_ = true;
}

TaskControlBlock *ReadyQueueManager::dequeue_highest() noexcept {
    uint64_t prio = bitmap_.get_highest_priority();
    if (prio == 0 && queues_[0].empty()) {
        return nullptr;
    }
    auto *tcb = queues_[prio].pop_front();
    if (queues_[prio].empty()) {
        bitmap_.clear(prio);
    }
    if (tcb) {
        tcb->rq_priority_ = 0;
        // The task is leaving the ready queue (it is being dispatched), so its
        // membership flag must be cleared.  Otherwise it keeps in_ready_queue_
        // true while not physically in the queue, and a later enqueue() (which
        // refuses already-queued TCBs) silently drops it — leaving a READY
        // task that next_task() can never find, wedging the scheduler in the
        // idle loop (observed as the `all` suite hanging at the atomic
        // context-switch tests).
        tcb->in_ready_queue_ = false;
    }
    return tcb;
}

TaskControlBlock *ReadyQueueManager::peek_highest() noexcept {
    uint64_t prio = bitmap_.get_highest_priority();
    if (prio == 0 && queues_[0].empty()) {
        return nullptr;
    }
    return queues_[prio].head();
}

void ReadyQueueManager::remove(TaskControlBlock &tcb,
                               uint64_t priority) noexcept {
    (void)priority;
    uint64_t actual = tcb.rq_priority_;
    queues_[actual].remove(tcb);
    if (queues_[actual].empty()) {
        bitmap_.clear(actual);
    }
    tcb.rq_priority_ = 0;
    tcb.in_ready_queue_ = false;
}

void ReadyQueueManager::move_priority(TaskControlBlock &tcb, uint64_t old_prio,
                                      uint64_t new_prio) noexcept {
    if (old_prio == new_prio)
        return;
    remove(tcb, old_prio);
    enqueue(tcb, new_prio);
}

void ReadyQueueManager::capture_pod(ReadyQueuePOD &out) const noexcept {
    out.bitmap_hi = bitmap_.raw_hi();
    out.bitmap_lo = bitmap_.raw_lo();
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        out.queue_heads[i] = reinterpret_cast<uint64_t>(queues_[i].head());
        out.queue_tails[i] = reinterpret_cast<uint64_t>(queues_[i].tail());
        out.queue_counts[i] = queues_[i].count();
    }
}

void ReadyQueueManager::restore_pod(const ReadyQueuePOD &src) noexcept {
    bitmap_.set_raw(src.bitmap_hi, src.bitmap_lo);
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        auto *h = reinterpret_cast<TaskControlBlock *>(src.queue_heads[i]);
        auto *t = reinterpret_cast<TaskControlBlock *>(src.queue_tails[i]);
        // The snapshot may reference TCBs that were later freed and recycled
        // onto other allocations (MemPool returns freed blocks to unrelated
        // callers).  Relinking a freed/reused pointer re-introduces a
        // corrupted node into the ready queue and produces the flaky GPF in
        // effective_priority()/pop_front().  Only restore queues whose head
        // and tail are still live, valid TCBs; otherwise drop the queue to
        // empty and let next_task()'s lazy rebuild reconstruct it from
        // all_tasks_ (which is itself safe_tcb-guarded).
        if ((h == nullptr || TaskControlBlock::is_valid(h)) &&
            (t == nullptr || TaskControlBlock::is_valid(t))) {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            queues_[i].set_raw(h, t, src.queue_counts[i]);
            // Keep in_ready_queue_ consistent with the restored queues so the
            // double-enqueue guard in enqueue() stays correct after a snapshot
            // restore (otherwise a restored-queue TCB keeps a stale
            // in_ready_queue_=false and a later set_task_ready re-enqueues it,
            // corrupting the intrusive links).
            for (auto *n = queues_[i].head(); n; n = n->runq_next_) {
                if (TaskControlBlock::is_valid(n))
                    n->in_ready_queue_ = true;
            }
        } else {
            queues_[i].reset();
            bitmap_.clear(i);
        }
    }
}

void ReadyQueueManager::clear_all() noexcept {
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        while (!queues_[i].empty()) {
            auto *t = queues_[i].pop_front();
            if (t) {
                t->in_ready_queue_ = false;
                t->rq_priority_ = 0;
            }
        }
    }
}

void ReadyQueueManager::reset() noexcept {
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        queues_[i].reset();
    }
    bitmap_.clear_all();
}
} // namespace kernel
