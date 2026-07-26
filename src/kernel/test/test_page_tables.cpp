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

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(page_tables_alloc_from_pool, "PRE: none | POST: none") {
    uint64_t pt_page = PMM::alloc_page_table();
    JARVIS_ASSERT(pt_page != 0);
    JARVIS_ASSERT(pt_page % arch::PAGE_SIZE == 0);
    PMM::free_page(pt_page);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_pool_multiple_allocs, "PRE: none | POST: none") {
    uint64_t pt1 = PMM::alloc_page_table();
    JARVIS_ASSERT(pt1 != 0);
    uint64_t pt2 = PMM::alloc_page_table();
    JARVIS_ASSERT(pt2 != 0);
    JARVIS_ASSERT(pt1 != pt2);
    PMM::free_page(pt1);
    PMM::free_page(pt2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_pool_size_configured, "PRE: none | POST: none") {
    JARVIS_ASSERT(CONFIG_PAGE_TABLE_POOL_SIZE > 0);
    JARVIS_ASSERT(CONFIG_PAGE_TABLE_POOL_SIZE >= 256);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_kernel_task_no_page_table,
            "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT(t->page_table_ == 0);
    JARVIS_ASSERT(t->page_table_shared_ == false);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_user_task_page_table_set, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT(t->page_table_ != 0);
    JARVIS_ASSERT(t->page_table_shared_ == false);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_free_pages_on_cleanup, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(t != nullptr);
    uint64_t page_table = t->page_table_;
    JARVIS_ASSERT(page_table != 0);
    t->cleanup();
    JARVIS_ASSERT(t->page_table_ == 0);
    JARVIS_ASSERT(t->user_stack_ == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(page_tables_max_process_pages_config, "PRE: none | POST: none") {
    JARVIS_ASSERT(CONFIG_MAX_PROCESS_PAGES > 0);
    JARVIS_ASSERT(CONFIG_MAX_PROCESS_PAGES >= 64);
    JARVIS_TEST_PASS();
}

void register_page_tables_tests() {
    Logger::info("Registering page tables tests");
    JARVIS_REGISTER_TEST(page_tables_alloc_from_pool);
    JARVIS_REGISTER_TEST(page_tables_pool_multiple_allocs);
    JARVIS_REGISTER_TEST(page_tables_pool_size_configured);
    JARVIS_REGISTER_TEST(page_tables_kernel_task_no_page_table);
    JARVIS_REGISTER_TEST(page_tables_user_task_page_table_set);
    JARVIS_REGISTER_TEST(page_tables_free_pages_on_cleanup);
    JARVIS_REGISTER_TEST(page_tables_max_process_pages_config);
}
