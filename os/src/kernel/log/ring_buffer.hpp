#pragma once

#include <types.hpp>

namespace kernel {
namespace log {

class RingBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 32768;

    void putchar(char c) {
        size_t w = write_pos_;
        size_t next = (w + 1) % BUFFER_SIZE;
        if (next == __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE))
            return;
        buf_[w] = c;
        __atomic_store_n(&write_pos_, next, __ATOMIC_RELEASE);
    }

    void puts(const char* s) {
        while (*s) putchar(*s++);
    }

    size_t read(char* dst, size_t size) {
        size_t r = __atomic_load_n(&read_pos_, __ATOMIC_RELAXED);
        size_t w = __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
        size_t written = 0;
        while (r != w && written < size) {
            *dst++ = buf_[r];
            r = (r + 1) % BUFFER_SIZE;
            ++written;
        }
        __atomic_store_n(&read_pos_, r, __ATOMIC_RELEASE);
        return written;
    }

    void clear() {
        __atomic_store_n(&read_pos_, __atomic_load_n(&write_pos_, __ATOMIC_RELAXED), __ATOMIC_RELEASE);
    }

    bool empty() const {
        return __atomic_load_n(&read_pos_, __ATOMIC_ACQUIRE) ==
               __atomic_load_n(&write_pos_, __ATOMIC_ACQUIRE);
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
