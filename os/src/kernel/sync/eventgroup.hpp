#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace sync {

class EventGroup {
public:
    static constexpr size_t MAX_WAITERS = 32;

    EventGroup() : bits_(0), wait_count_(0) {}
    void init();

    void set_bits(uint64_t bits);
    void clear_bits(uint64_t bits);
    uint64_t get_bits() const { return bits_; }

    uint64_t wait_bits(uint64_t bits, bool clear_on_exit = false);
    bool try_wait_bits(uint64_t bits);

private:
    uint64_t bits_;
    struct EventWaiter {
        TaskControlBlock* task;
        uint64_t wanted_bits;
        bool clear_on_exit;
    };
    EventWaiter waiters_[MAX_WAITERS];
    size_t wait_count_;

    bool add_waiter(TaskControlBlock* task, uint64_t wanted, bool clear);
    void wake_matching();
};

}
}
