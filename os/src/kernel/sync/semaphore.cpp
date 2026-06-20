#include <kernel/sync/semaphore.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <assert.hpp>

namespace kernel {
namespace sync {

void Semaphore::init(uint64_t initial, uint64_t max) {
    count_ = initial;
    max_count_ = max;
    waiter_count_ = 0;
}

bool Semaphore::add_waiter(TaskControlBlock* task) {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ >= MAX_WAITERS) return false;
    waiters_[waiter_count_++] = task;
    return true;
}

void Semaphore::wake_one() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ == 0) return;

    if (waiter_count_ > 1) {
        size_t best = 0;
        for (size_t i = 1; i < waiter_count_; ++i) {
            if (waiters_[i]->priority > waiters_[best]->priority) best = i;
        }
        if (waiters_[best]->state != TaskState::TERMINATED)
            waiters_[best]->state = TaskState::READY;
        waiters_[best] = waiters_[--waiter_count_];
    } else {
        if (waiters_[0]->state != TaskState::TERMINATED)
            waiters_[0]->state = TaskState::READY;
        waiter_count_ = 0;
    }
}

void Semaphore::wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    auto* task = Scheduler::current_task();
    if (!task) return;

    if (count_ > 0) {
        --count_;
        return;
    }

    bool added = add_waiter(task);
    ENSURE(added);

    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();
}

bool Semaphore::try_wait() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (count_ > 0) {
        --count_;
        return true;
    }
    return false;
}

void Semaphore::post() {
    SpinLockGuard<SpinLock> guard(lock_);
    if (waiter_count_ > 0) {
        wake_one();
    } else if (count_ < max_count_) {
        ++count_;
    }
}

}
}
