#include <kernel/sync/mutex.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace sync {

void Mutex::init() {
    owner_ = nullptr;
    holder_priority_ = 0;
    lock_count_ = 0;
    wait_count_ = 0;
}

bool Mutex::add_waiter(TaskControlBlock* task) {
    if (wait_count_ >= MAX_WAITERS) return false;
    waiters_[wait_count_++] = task;
    return true;
}

void Mutex::wake_one() {
    if (wait_count_ == 0) return;

    size_t best = 0;
    for (size_t i = 1; i < wait_count_; ++i) {
        if (waiters_[i]->priority > waiters_[best]->priority) best = i;
    }

    if (waiters_[best]->state != TaskState::TERMINATED)
        waiters_[best]->state = TaskState::READY;
    waiters_[best] = waiters_[--wait_count_];
}

void Mutex::inherit_priority(TaskControlBlock* waiter) {
    if (!owner_) return;
    if (waiter->priority > owner_->priority) {
        holder_priority_ = owner_->priority;
        owner_->priority = waiter->priority;
    }
}

void Mutex::restore_priority() {
    if (!owner_ || holder_priority_ == 0) return;
    owner_->priority = holder_priority_;
    holder_priority_ = 0;
}

void Mutex::lock() {
    auto* task = Scheduler::current_task();
    if (!task) return;

    if (owner_ == task) {
        ++lock_count_;
        return;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        return;
    }

    inherit_priority(task);

    if (!add_waiter(task)) return;

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

bool Mutex::try_lock() {
    auto* task = Scheduler::current_task();
    if (!task) return false;

    if (owner_ == task) {
        ++lock_count_;
        return true;
    }

    if (owner_ == nullptr) {
        owner_ = task;
        lock_count_ = 1;
        return true;
    }

    return false;
}

void Mutex::unlock() {
    auto* task = Scheduler::current_task();
    if (!task || owner_ != task) return;

    if (lock_count_ > 1) {
        --lock_count_;
        return;
    }

    owner_ = nullptr;
    lock_count_ = 0;

    restore_priority();

    if (wait_count_ > 0) {
        wake_one();
    }
}

}
}
