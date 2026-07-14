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

/// @file test_memory_safety.cpp
/// @brief Memory safety boundary tests.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies MemPool::free(nullptr) is a safe no-op.
// Input: MemPool::free(nullptr)
// Expect: No crash, returns immediately
// Depends: MemPool
JARVIS_TEST(memory_safety_mempool_free_null, "PRE: none | POST: none") {
    MemPool::free(nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies MemPool::alloc rejects sizes larger than any pool.
// The largest pool is 4480 bytes. Allocating 4481 should fail.
// Input: MemPool::alloc(4481)
// Expect: Returns nullptr
// Depends: MemPool
JARVIS_TEST(memory_safety_mempool_alloc_large_rejected,
            "PRE: none | POST: none") {
    void *p = MemPool::alloc(4481);
    JARVIS_ASSERT(p == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies MemPool alloc/free at exact pool size boundaries
// returns valid pointers and cleans up correctly.
// Input: Alloc at 16, 32, 64, 128, 256, 512, 1024, 2048, 4480 bytes
// Expect: All return non-null pointers, free succeeds
// Depends: MemPool
JARVIS_TEST(memory_safety_mempool_exact_edge_sizes, "PRE: none | POST: none") {
    static const size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4480};
    void *ptrs[9];
    for (size_t i = 0; i < 9; ++i) {
        ptrs[i] = MemPool::alloc(sizes[i]);
        JARVIS_ASSERT_FMT(ptrs[i] != nullptr, "MemPool::alloc(%zu) failed",
                          sizes[i]);
    }
    for (size_t i = 0; i < 9; ++i) {
        MemPool::free(ptrs[i]);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PMM::free_page(0) is a safe no-op. Page index 0
// either refers to a reserved BIOS page (bitmap set) or an unallocated page
// (bitmap clear) — either way the call must not panic.
// Input: PMM::free_page(0)
// Expect: No crash or assertion
// Depends: PMM
JARVIS_TEST(memory_safety_pmm_free_zero, "PRE: none | POST: none") {
    PMM::free_page(0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PMM::free_page with a page beyond total memory is safe.
// Input: PMM::free_page(0xFFFFFFFFFFFFF000)
// Expect: Guards against out-of-bounds bitmap access, returns without crash
// Depends: PMM
JARVIS_TEST(memory_safety_pmm_free_beyond_total, "PRE: none | POST: none") {
    uint64_t huge = 0xFFFFFFFFFFFFF000ULL;
    PMM::free_page(huge);
    JARVIS_TEST_PASS();
}

void register_memory_safety_tests() {
    Logger::info("Registering memory safety tests");
    JARVIS_REGISTER_TEST(memory_safety_mempool_free_null);
    JARVIS_REGISTER_TEST(memory_safety_mempool_alloc_large_rejected);
    JARVIS_REGISTER_TEST(memory_safety_mempool_exact_edge_sizes);
    JARVIS_REGISTER_TEST(memory_safety_pmm_free_zero);
    JARVIS_REGISTER_TEST(memory_safety_pmm_free_beyond_total);
}
