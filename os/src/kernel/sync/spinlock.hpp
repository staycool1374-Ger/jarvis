#pragma once

#include <kernel/arch/io.hpp>

namespace kernel {
namespace sync {

class SpinLock {
public:
    SpinLock() = default;

    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&)                 = delete;
    SpinLock& operator=(SpinLock&&)      = delete;

    void lock() noexcept {
        while (__atomic_exchange_n(&locked_, 1, __ATOMIC_ACQUIRE)) {
            arch::pause();
        }
    }

    void unlock() noexcept {
        __atomic_store_n(&locked_, 0, __ATOMIC_RELEASE);
    }

    bool try_lock() noexcept {
        return !__atomic_exchange_n(&locked_, 1, __ATOMIC_ACQUIRE);
    }

private:
    int locked_ = 0;
};

} // namespace sync
} // namespace kernel
