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

/// @file test_mempool.cpp
/// @brief MemPool allocator tests.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/memory/pmm.hpp>

using namespace kernel;

JARVIS_TEST(mempool_alloc_free, "PRE: none | POST: none") {
    size_t before = MemPool::pool_free_count(0);
    void *p = MemPool::alloc(16);
    JARVIS_ASSERT(p != nullptr);
    JARVIS_ASSERT(MemPool::pool_free_count(0) == before - 1);
    MemPool::free(p);
    JARVIS_ASSERT(MemPool::pool_free_count(0) == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_large_alloc, "PRE: none | POST: none") {
    void *p = MemPool::alloc(4096);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_fragmentation, "PRE: none | POST: none") {
    static const size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 8192};
    for (size_t s = 0; s < 9; ++s) {
        size_t bytes = sizes[s];
        static const int ALLOCS = 20;
        void *ptrs[ALLOCS] = {};
        int count = 0;
        for (int i = 0; i < ALLOCS; ++i) {
            ptrs[i] = MemPool::alloc(bytes);
            if (!ptrs[i]) break;
            count = i + 1;
            __builtin_memset(ptrs[i], 0xA5, bytes);
        }
        for (int i = count - 1; i >= 0; --i)
            MemPool::free(ptrs[i]);
    }
    void *p = MemPool::alloc(64);
    JARVIS_ASSERT(p != nullptr);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_reuse, "PRE: none | POST: none") {
    void *a = MemPool::alloc(32);
    JARVIS_ASSERT(a != nullptr);
    MemPool::free(a);
    void *b = MemPool::alloc(32);
    JARVIS_ASSERT(b == a);
    MemPool::free(b);
    JARVIS_TEST_PASS();
}

void register_mempool_tests() {
    Logger::info("Registering MemPool tests");
    JARVIS_REGISTER_TEST(mempool_alloc_free);
    JARVIS_REGISTER_TEST(mempool_large_alloc);
    JARVIS_REGISTER_TEST(mempool_fragmentation);
    JARVIS_REGISTER_TEST(mempool_reuse);
}
