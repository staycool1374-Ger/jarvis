/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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

#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

struct alignas(16) TestStruct {
    uint64_t a;
    uint64_t b;
    char buf[32];
};

static uint8_t g_static_buf[sizeof(TestStruct)];

// Runmode: kernel
// Testidea: Placement new into static buffer works without heap allocation.
// This verifies placement new compiles and functions correctly in the
// kernel (which disables global operator new).  The elimination of
// ::operator new is a compile-time/link-time property — this test
// confirms that placement new (the only form available) works.
// Input: Placement-new a TestStruct into static buffer, write fields.
// Expect: Fields round-trip correctly.
// Depends: placement-new syntax, compiler support
JARVIS_TEST(no_op_new_placement_new_in_static, "PRE: none | POST: none") {
    auto *obj = ::new (g_static_buf) TestStruct();
    JARVIS_ASSERT(obj != nullptr);
    JARVIS_ASSERT(reinterpret_cast<uint64_t>(obj) >=
                  reinterpret_cast<uint64_t>(g_static_buf));
    JARVIS_ASSERT(reinterpret_cast<uint64_t>(obj) <
                  reinterpret_cast<uint64_t>(g_static_buf) + sizeof(g_static_buf));
    obj->a = 42;
    obj->b = 99;
    JARVIS_ASSERT(obj->a == 42);
    JARVIS_ASSERT(obj->b == 99);
    obj->~TestStruct();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: MemPool alloc/free cycle works (replacement for ::new/::delete).
// In the kernel, ::operator new is disabled — all dynamic allocation goes
// through MemPool.  This test verifies the basic MemPool alloc/write/free
// cycle is functional.
// Input: Allocate 64 bytes from MemPool, fill with 0xAA, free.
// Expect: Non-null pointer, MemPool::contains() true, content persists.
// Depends: MemPool
JARVIS_TEST(no_op_new_mempool_alloc_free_cycle, "PRE: none | POST: none") {
    void *p = MemPool::alloc(64);
    JARVIS_ASSERT(p != nullptr);
    JARVIS_ASSERT(MemPool::contains(p));
    __builtin_memset(p, 0xAA, 64);
    auto *buf = static_cast<uint8_t *>(p);
    JARVIS_ASSERT(buf[0] == 0xAA);
    JARVIS_ASSERT(buf[63] == 0xAA);
    MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(no_op_new_mempool_multiple_sizes, "PRE: none | POST: none") {
    void *p16 = MemPool::alloc(16);
    void *p32 = MemPool::alloc(32);
    void *p64 = MemPool::alloc(64);
    JARVIS_ASSERT(p16 != nullptr);
    JARVIS_ASSERT(p32 != nullptr);
    JARVIS_ASSERT(p64 != nullptr);
    JARVIS_ASSERT(p16 != p32);
    JARVIS_ASSERT(p32 != p64);
    MemPool::free(p16);
    MemPool::free(p32);
    MemPool::free(p64);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(no_op_new_placement_new_array_static, "PRE: none | POST: none") {
    static uint8_t arr_buf[256];
    auto *arr = ::new (arr_buf) uint64_t[32];
    JARVIS_ASSERT(arr != nullptr);
    for (int i = 0; i < 32; ++i)
        arr[i] = i * 3;
    for (int i = 0; i < 32; ++i)
        JARVIS_ASSERT(arr[i] == static_cast<uint64_t>(i * 3));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(no_op_new_placement_new_nested_struct, "PRE: none | POST: none") {
    static uint8_t nested_buf[256];
    struct Inner {
        uint64_t x;
    };
    struct Outer {
        Inner inner;
        uint64_t y;
    };
    auto *obj = ::new (nested_buf) Outer();
    JARVIS_ASSERT(obj != nullptr);
    JARVIS_ASSERT(reinterpret_cast<uint64_t>(obj) >=
                  reinterpret_cast<uint64_t>(nested_buf));
    JARVIS_ASSERT(reinterpret_cast<uint64_t>(obj) <
                  reinterpret_cast<uint64_t>(nested_buf) + sizeof(nested_buf));
    obj->inner.x = 10;
    obj->y = 20;
    JARVIS_ASSERT(obj->inner.x == 10);
    JARVIS_ASSERT(obj->y == 20);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(no_op_new_mempool_reuse_after_free, "PRE: none | POST: none") {
    void *p1 = MemPool::alloc(128);
    JARVIS_ASSERT(p1 != nullptr);
    __builtin_memset(p1, 0xFF, 128);
    MemPool::free(p1);
    void *p2 = MemPool::alloc(128);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p2 == p1);
    MemPool::free(p2);
    JARVIS_TEST_PASS();
}

void register_no_op_new_tests() {
    Logger::info("Registering no-operator-new tests");
    JARVIS_REGISTER_TEST(no_op_new_placement_new_in_static);
    JARVIS_REGISTER_TEST(no_op_new_mempool_alloc_free_cycle);
    JARVIS_REGISTER_TEST(no_op_new_mempool_multiple_sizes);
    JARVIS_REGISTER_TEST(no_op_new_placement_new_array_static);
    JARVIS_REGISTER_TEST(no_op_new_placement_new_nested_struct);
    JARVIS_REGISTER_TEST(no_op_new_mempool_reuse_after_free);
}
