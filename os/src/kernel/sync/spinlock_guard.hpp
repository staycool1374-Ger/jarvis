#pragma once

/// @brief RAII lock guard for any lock type with lock()/unlock() methods.
template<typename Lock>
class [[nodiscard]] SpinLockGuard {
public:
    explicit SpinLockGuard(Lock& lock) noexcept : lock_(lock) {
        lock_.lock();
    }

    ~SpinLockGuard() noexcept {
        lock_.unlock();
    }

    SpinLockGuard(const SpinLockGuard&)            = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
    SpinLockGuard(SpinLockGuard&&)                 = delete;
    SpinLockGuard& operator=(SpinLockGuard&&)      = delete;

private:
    Lock& lock_;
};
