#include <kernel/task/ready_queue_manager.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

void ReadyQueueManager::enqueue(TaskControlBlock& tcb, uint64_t priority) noexcept {
    queues_[priority].push_back(tcb);
    bitmap_.set(priority);
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
    return tcb;
}

void ReadyQueueManager::remove(TaskControlBlock& tcb, uint64_t priority) noexcept {
    queues_[priority].remove(tcb);
    if (queues_[priority].empty()) {
        bitmap_.clear(priority);
    }
}

void ReadyQueueManager::move_priority(TaskControlBlock& tcb, uint64_t old_prio, uint64_t new_prio) noexcept {
    if (old_prio == new_prio) return;
    remove(tcb, old_prio);
    enqueue(tcb, new_prio);
}

void ReadyQueueManager::capture_state(uint64_t* out_bitmap_hi, uint64_t* out_bitmap_lo) const noexcept {
    *out_bitmap_hi = bitmap_.raw_hi();
    *out_bitmap_lo = bitmap_.raw_lo();
}

void ReadyQueueManager::restore_state(uint64_t bitmap_hi, uint64_t bitmap_lo) noexcept {
    bitmap_.set_raw(bitmap_hi, bitmap_lo);
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
