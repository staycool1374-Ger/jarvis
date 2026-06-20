#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel {
namespace sync {

static constexpr uint64_t NOTIFY_INVALID = 0;

class Notify {
public:
    Notify() : notify_value_(0), waiter_(nullptr), initialized_(false) {}
    /// @brief Initialize the notification object.
    void init();

    /// @brief Signal a waiter with a value, waking it.
    /// @param value The value to deliver to the waiter.
    void notify(uint64_t value);
    /// @brief Block until notified.
    /// @return The value passed by the notifier.
    uint64_t wait();
    /// @brief Check if notified without blocking.
    /// @param[out] value Receives the notification value if available.
    /// @return true if a notification was pending.
    bool try_wait(uint64_t* value);

    uint64_t value() const { return notify_value_; }

private:
    SpinLock lock_;
    uint64_t notify_value_;
    TaskControlBlock* waiter_;
    bool initialized_;
};

}
}
