#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace sync {

class Semaphore {
public:
    static constexpr size_t MAX_WAITERS = 32;

    Semaphore()
        : count_(0)
        , max_count_(1)
        , waiter_count_(0)
        {}
    void init(uint64_t initial, uint64_t max = 0xFFFFFFFF);

    void wait();
    bool try_wait();
    void post();

    uint64_t value() const { return count_; }

private:
    uint64_t count_;
    uint64_t max_count_;
    TaskControlBlock* waiters_[MAX_WAITERS];
    size_t waiter_count_;

    bool add_waiter(TaskControlBlock* task);
    void wake_one();
};

}
}
