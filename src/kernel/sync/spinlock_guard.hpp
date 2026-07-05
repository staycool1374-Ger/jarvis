/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file spinlock_guard.hpp
/// @brief RAII lock guard template for any lock with lock()/unlock().

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
