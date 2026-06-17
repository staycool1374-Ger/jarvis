#include <kernel/sync/notify.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/irq_guard.hpp>

namespace kernel {
namespace sync {

void Notify::init() {
    notify_value_ = 0;
    waiter_ = nullptr;
    initialized_ = true;
}

void Notify::notify(uint64_t value) {
    arch::IrqGuard guard;
    notify_value_ = value;
    if (waiter_) {
        if (waiter_->state != TaskState::TERMINATED)
            waiter_->state = TaskState::READY;
        waiter_ = nullptr;
    }
}

uint64_t Notify::wait() {
    arch::IrqGuard guard;
    auto* task = Scheduler::current_task();
    if (!task) return 0;

    if (waiter_ != nullptr) return 0;

    waiter_ = task;
    task->state = TaskState::BLOCKED;
    Scheduler::reschedule();

    return notify_value_;
}

bool Notify::try_wait(uint64_t* value) {
    arch::IrqGuard guard;
    if (waiter_ == nullptr && value && notify_value_ != 0) {
        *value = notify_value_;
        notify_value_ = 0;
        return true;
    }
    return false;
}

}
}
