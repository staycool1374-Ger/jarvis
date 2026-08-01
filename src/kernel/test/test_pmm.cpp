/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
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

/// @file test_pmm.cpp
/// @brief Physical memory manager tests.

#include <test.hpp>
#include <logger.hpp>
#include <constants.hpp>
#include <kernel/memory/pmm.hpp>

using namespace kernel;

JARVIS_TEST(pmm_alloc_free, "PRE: none | POST: none") {
    uint64_t before = PMM::free_memory();
    uint64_t p1 = PMM::alloc_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(PMM::free_memory() == before - 4096);
    PMM::free_page(p1);
    JARVIS_ASSERT(PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_contiguous, "PRE: none | POST: none") {
    uint64_t before = PMM::free_memory();
    uint64_t pages = PMM::alloc_contiguous(4);
    JARVIS_ASSERT(pages != 0);
    JARVIS_ASSERT(PMM::free_memory() <= before - 4 * 4096);
    for (size_t i = 0; i < 4; ++i)
        PMM::free_page(pages + i * 4096);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_user_alloc, "PRE: none | POST: none") {
    uint64_t p = PMM::alloc_user_page();
    JARVIS_ASSERT(p != 0);
    JARVIS_ASSERT(PMM::is_user_page(p));
    PMM::free_page(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_total_memory, "PRE: none | POST: none") {
    JARVIS_ASSERT(PMM::free_memory() > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_page_table, "PRE: none | POST: none") {
    uint64_t pt = PMM::alloc_page_table();
    JARVIS_ASSERT(pt != 0);
#if defined(CONFIG_ARCH_X86_64)
    JARVIS_ASSERT(pt < 128ULL * 1024 * 1024);
#endif
    PMM::free_page(pt);
    JARVIS_TEST_PASS();
}

void register_pmm_tests() {
    Logger::info("Registering PMM tests");
    JARVIS_REGISTER_TEST(pmm_alloc_free);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous);
    JARVIS_REGISTER_TEST(pmm_user_alloc);
    JARVIS_REGISTER_TEST(pmm_total_memory);
    JARVIS_REGISTER_TEST(pmm_alloc_page_table);
}
