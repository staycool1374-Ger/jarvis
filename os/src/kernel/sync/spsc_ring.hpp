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

#include <types.hpp>

template<typename T, size_t N>
class SPSCRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be power of 2");

    static constexpr size_t MASK = N - 1;

public:
    bool try_push(const T& item) {
        size_t h = head_;
        size_t t = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
        size_t next = (h + 1) & MASK;
        if (next == t)
            return false;
        data_[h] = item;
        __atomic_store_n(&head_, next, __ATOMIC_RELEASE);
        return true;
    }

    bool try_pop(T& item) {
        size_t t = tail_;
        size_t h = __atomic_load_n(&head_, __ATOMIC_ACQUIRE);
        if (t == h)
            return false;
        item = data_[t];
        __atomic_store_n(&tail_, (t + 1) & MASK, __ATOMIC_RELEASE);
        return true;
    }

    bool empty() const {
        return __atomic_load_n(&head_, __ATOMIC_ACQUIRE) ==
               __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
    }

    void reset() {
        __atomic_store_n(&head_, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&tail_, 0, __ATOMIC_RELAXED);
    }

private:
    alignas(64) size_t head_ = 0;
    alignas(64) size_t tail_ = 0;
    T data_[N] = {};
};
