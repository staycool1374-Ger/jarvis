#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace sync {

class Mutex {
public:
    static constexpr size_t MAX_WAITERS = 32;

    Mutex() : owner_(nullptr), holder_priority_(0), lock_count_(0), wait_count_(
        0) {}
    void init();

    void lock();
    bool try_lock();
    void unlock();

    bool is_locked() const { return owner_ != nullptr; }
    TaskControlBlock* owner() const { return owner_; }

private:
    TaskControlBlock* owner_;
    uint64_t holder_priority_;
    uint64_t lock_count_;
    TaskControlBlock* waiters_[MAX_WAITERS];
    size_t wait_count_;

    bool add_waiter(TaskControlBlock& task);
    void wake_one();
    void inherit_priority(TaskControlBlock& waiter);
    void restore_priority();
};

}
}
