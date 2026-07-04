#include <kernel/task/ready_queue_manager.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

void ReadyQueueManager::enqueue(TaskControlBlock& tcb, uint64_t priority) noexcept {
    queues_[priority].push_back(tcb);
    bitmap_.set(priority);
    tcb.rq_priority_ = priority;
}

TaskControlBlock* ReadyQueueManager::dequeue_highest() noexcept {
    uint64_t prio = bitmap_.get_highest_priority();
    if (prio == 0 && queues_[0].empty()) {
        return nullptr;
    }
    auto* tcb = queues_[prio].pop_front();
    if (queues_[prio].empty()) {
        bitmap_.clear(prio);
    }
    if (tcb) tcb->rq_priority_ = 0;
    return tcb;
}

TaskControlBlock* ReadyQueueManager::peek_highest() noexcept {
    uint64_t prio = bitmap_.get_highest_priority();
    if (prio == 0 && queues_[0].empty()) {
        return nullptr;
    }
    return queues_[prio].head();
}

void ReadyQueueManager::remove(TaskControlBlock& tcb, uint64_t priority) noexcept {
    (void)priority;
    uint64_t actual = tcb.rq_priority_;
    queues_[actual].remove(tcb);
    if (queues_[actual].empty()) {
        bitmap_.clear(actual);
    }
    tcb.rq_priority_ = 0;
}

void ReadyQueueManager::move_priority(TaskControlBlock& tcb, uint64_t old_prio, uint64_t new_prio) noexcept {
    if (old_prio == new_prio) return;
    remove(tcb, old_prio);
    enqueue(tcb, new_prio);
}

void ReadyQueueManager::capture_pod(ReadyQueuePOD& out) const noexcept {
    out.bitmap_hi = bitmap_.raw_hi();
    out.bitmap_lo = bitmap_.raw_lo();
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        out.queue_heads[i] = reinterpret_cast<uint64_t>(queues_[i].head());
        out.queue_tails[i] = reinterpret_cast<uint64_t>(queues_[i].tail());
        out.queue_counts[i] = queues_[i].count();
    }
}

void ReadyQueueManager::restore_pod(const ReadyQueuePOD& src) noexcept {
    bitmap_.set_raw(src.bitmap_hi, src.bitmap_lo);
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        // Restore queue head/tail from raw addresses (TCBs are in-place)
        queues_[i].set_raw(
            reinterpret_cast<TaskControlBlock*>(src.queue_heads[i]),
            reinterpret_cast<TaskControlBlock*>(src.queue_tails[i]),
            src.queue_counts[i]);
    }
}

void ReadyQueueManager::clear_all() noexcept {
    for (uint64_t i = 0; i <= CONFIG_PRIORITY_CEILING; ++i) {
        while (!queues_[i].empty()) {
            queues_[i].pop_front();
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
