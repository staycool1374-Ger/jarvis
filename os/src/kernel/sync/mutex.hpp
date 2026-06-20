#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel {
namespace sync {

class Mutex {
public:
    static constexpr size_t MAX_WAITERS = 32;

    Mutex() : owner_(nullptr), holder_priority_(0), lock_count_(0), wait_count_(
        0) {}
    /// @brief Initialize the mutex to unlocked state.
    void init();

    /// @brief Acquire the mutex, blocking until available.
    void lock();
    /// @brief Attempt to acquire the mutex without blocking.
    /// @return true if the lock was acquired.
    bool try_lock();
    /// @brief Release the mutex, waking the next waiter if any.
    void unlock();

    bool is_locked() const { return owner_ != nullptr; }
    TaskControlBlock* owner() const { return owner_; }

private:
    SpinLock lock_;
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
