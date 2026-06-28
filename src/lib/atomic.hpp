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

namespace kernel {

// Full memory barrier (sequential consistency)
inline void atomic_fence() {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// Acquire fence
inline void atomic_acquire_fence() {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

// Release fence
inline void atomic_release_fence() {
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

// Atomic load (non-volatile pointer)
template<typename T>
inline T atomic_load(const T* ptr, int memorder = __ATOMIC_SEQ_CST) {
    T val;
    __atomic_load(ptr, &val, memorder);
    return val;
}

// Atomic load (volatile pointer)
template<typename T>
inline T atomic_load(const volatile T* ptr, int memorder = __ATOMIC_SEQ_CST) {
    T val;
    __atomic_load(ptr, &val, memorder);
    return val;
}

// Atomic store (non-volatile pointer)
template<typename T>
inline void atomic_store(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    __atomic_store(ptr, &val, memorder);
}

// Atomic store (volatile pointer)
template<typename T>
inline void atomic_store(volatile T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    __atomic_store(ptr, &val, memorder);
}

// Atomic exchange
template<typename T>
inline T atomic_exchange(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    T old;
    __atomic_exchange(ptr, &val, &old, memorder);
    return old;
}

// Atomic compare-exchange (strong)
template<typename T>
inline bool atomic_compare_exchange(T* ptr, T* expected, T desired,
                                    int success_memorder = __ATOMIC_SEQ_CST,
                                    int failure_memorder = __ATOMIC_SEQ_CST) {
    return __atomic_compare_exchange(ptr, expected, &desired, false,
                                     success_memorder, failure_memorder);
}

// Atomic add (fetch-and-add)
template<typename T>
inline T atomic_fetch_add(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    return __atomic_fetch_add(ptr, val, memorder);
}

// Atomic sub
template<typename T>
inline T atomic_fetch_sub(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    return __atomic_fetch_sub(ptr, val, memorder);
}

// Atomic and
template<typename T>
inline T atomic_fetch_and(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    return __atomic_fetch_and(ptr, val, memorder);
}

// Atomic or
template<typename T>
inline T atomic_fetch_or(T* ptr, T val, int memorder = __ATOMIC_SEQ_CST) {
    return __atomic_fetch_or(ptr, val, memorder);
}

} // namespace kernel
