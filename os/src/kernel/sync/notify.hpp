#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace sync {

static constexpr uint64_t NOTIFY_INVALID = 0;

class Notify {
public:
    Notify() : notify_value_(0), waiter_(nullptr), initialized_(false) {}
    void init();

    void notify(uint64_t value);
    uint64_t wait();
    bool try_wait(uint64_t* value);

    uint64_t value() const { return notify_value_; }

private:
    uint64_t notify_value_;
    TaskControlBlock* waiter_;
    bool initialized_;
};

}
}
