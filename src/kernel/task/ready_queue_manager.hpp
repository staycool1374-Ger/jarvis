#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>
#include <kernel/task/priority_map.hpp>
#include <kernel/task/task_queue.hpp>
#include <kernel/task/task_fwd.hpp>

namespace kernel {

class ReadyQueueManager {
    PriorityMap bitmap_;
    TaskQueue queues_[CONFIG_PRIORITY_CEILING + 1];

public:
    void enqueue(TaskControlBlock& tcb, uint64_t priority) noexcept;
    TaskControlBlock* dequeue_highest() noexcept;
    void remove(TaskControlBlock& tcb, uint64_t priority) noexcept;
    void move_priority(TaskControlBlock& tcb, uint64_t old_prio, uint64_t new_prio) noexcept;

    bool has_ready() const noexcept { return !bitmap_.empty(); }
    uint64_t highest_ready_priority() const noexcept { return bitmap_.get_highest_priority(); }
    const PriorityMap& bitmap() const noexcept { return bitmap_; }
    const TaskQueue& queue(uint64_t prio) const noexcept { return queues_[prio]; }

    void capture_state(uint64_t* out_bitmap_hi, uint64_t* out_bitmap_lo) const noexcept;
    void restore_state(uint64_t bitmap_hi, uint64_t bitmap_lo) noexcept;
    void clear_all() noexcept;
    /// @brief Resets all queues without following pointers.
    ///        Safe to call with dangling pointers (e.g., during test isolation).
    void reset() noexcept;
};

} // namespace kernel
