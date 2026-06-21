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
