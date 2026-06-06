#include <kernel/sync/eventgroup.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace sync {

void EventGroup::init() {
    bits_ = 0;
    wait_count_ = 0;
}

void EventGroup::set_bits(uint64_t bits) {
    bits_ |= bits;
    wake_matching();
}

void EventGroup::clear_bits(uint64_t bits) {
    bits_ &= ~bits;
}

bool EventGroup::add_waiter(TaskControlBlock* task, uint64_t wanted, bool clear) {
    if (wait_count_ >= MAX_WAITERS) return false;
    waiters_[wait_count_].task = task;
    waiters_[wait_count_].wanted_bits = wanted;
    waiters_[wait_count_].clear_on_exit = clear;
    ++wait_count_;
    return true;
}

void EventGroup::wake_matching() {
    for (size_t i = 0; i < wait_count_;) {
        if ((bits_ & waiters_[i].wanted_bits) == waiters_[i].wanted_bits) {
            if (waiters_[i].clear_on_exit) {
                bits_ &= ~waiters_[i].wanted_bits;
            }
            waiters_[i].task->state = TaskState::READY;
            waiters_[i] = waiters_[--wait_count_];
        } else {
            ++i;
        }
    }
}

uint64_t EventGroup::wait_bits(uint64_t bits, bool clear_on_exit) {
    auto* task = Scheduler::current_task();
    if (!task) return bits_;

    if ((bits_ & bits) == bits) {
        if (clear_on_exit) bits_ &= ~bits;
        return bits_;
    }

    if (!add_waiter(task, bits, clear_on_exit)) return bits_;

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    return bits_;
}

bool EventGroup::try_wait_bits(uint64_t bits) {
    return (bits_ & bits) == bits;
}

}
}
