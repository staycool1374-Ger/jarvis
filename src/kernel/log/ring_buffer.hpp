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
#include <lib/atomic.hpp>

namespace kernel {
namespace log {

class RingBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 32768;

    void putchar(char c) {
        size_t w = write_pos_;
        size_t next = (w + 1) % BUFFER_SIZE;
        if (next == atomic_load(&read_pos_, __ATOMIC_ACQUIRE))
            return;
        buf_[w] = c;
        atomic_store(&write_pos_, next, __ATOMIC_RELEASE);
    }

    void puts(const char* s) {
        while (*s) putchar(*s++);
    }

    size_t read(char* dst, size_t size) {
        size_t r = atomic_load(&read_pos_, __ATOMIC_RELAXED);
        size_t w = atomic_load(&write_pos_, __ATOMIC_ACQUIRE);
        size_t written = 0;
        while (r != w && written < size) {
            *dst++ = buf_[r];
            r = (r + 1) % BUFFER_SIZE;
            ++written;
        }
        atomic_store(&read_pos_, r, __ATOMIC_RELEASE);
        return written;
    }

    void clear() {
        atomic_store(&read_pos_, atomic_load(&write_pos_, __ATOMIC_RELAXED), __ATOMIC_RELEASE);
    }

    bool empty() const {
        return atomic_load(&read_pos_, __ATOMIC_ACQUIRE) ==
               atomic_load(&write_pos_, __ATOMIC_ACQUIRE);
    }

    RingBuffer() : buf_{} {}

private:
    char buf_[BUFFER_SIZE];
    alignas(64) volatile size_t write_pos_ = 0;
    alignas(64) volatile size_t read_pos_ = 0;
};

extern RingBuffer g_klog;

} // namespace log
} // namespace kernel
