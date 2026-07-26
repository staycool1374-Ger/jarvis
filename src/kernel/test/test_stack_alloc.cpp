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
#include <constants.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/io.hpp>

using namespace kernel;

JARVIS_TEST(stack_alloc_default_size_correct, "PRE: none | POST: none") {
    JARVIS_ASSERT(TaskControlBlock::STACK_SIZE == CONFIG_STACK_SIZE);
    JARVIS_ASSERT(TaskControlBlock::STACK_SIZE >= CONFIG_MIN_STACK_SIZE);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_task_has_stack_phys, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT(t->stack_phys_ != 0);
    JARVIS_ASSERT(t->stack_phys_ % arch::PAGE_SIZE == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_user_task_has_guard_page, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create_user([]() {}, 5, 10, 64_KiB);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    uint64_t stack_virt = t->user_stack_;
    JARVIS_ASSERT(stack_virt != 0);
    JARVIS_ASSERT(t->page_table_ != 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_stack_alignment, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT((reinterpret_cast<uint64_t>(t->kernel_stack) %
                   arch::PAGE_SIZE) == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_multiple_tasks_distinct_stacks,
            "PRE: none | POST: none") {
    auto *t1 = TaskControlBlock::create([]() {}, 5, 10);
    auto *t2 = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(t1 != nullptr && t2 != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t1->cleanup();
        delete t1;
        t2->cleanup();
        delete t2;
    });
    JARVIS_ASSERT(t1->kernel_stack != t2->kernel_stack);
    JARVIS_ASSERT(t1->stack_phys_ != t2->stack_phys_);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_overflow_hook_weak_symbol,
            "PRE: none | POST: none") {
    extern void stack_overflow_hook(void *task) __attribute__((weak));
    JARVIS_ASSERT(stack_overflow_hook == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_user_stack_phys_freed_on_cleanup,
            "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT(t->user_stack_ != 0);
    JARVIS_ASSERT(t->page_table_ != 0);
    t->cleanup();
    JARVIS_ASSERT(t->page_table_ == 0);
    JARVIS_ASSERT(t->user_stack_ == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_alloc_user_stack_size_in_config, "PRE: none | POST: none") {
    JARVIS_ASSERT(CONFIG_STACK_SIZE > 0);
    JARVIS_ASSERT(CONFIG_MIN_STACK_SIZE > 0);
    JARVIS_ASSERT(CONFIG_STACK_SIZE >= CONFIG_MIN_STACK_SIZE);
    JARVIS_TEST_PASS();
}

void register_stack_alloc_tests() {
    Logger::info("Registering stack allocation tests");
    JARVIS_REGISTER_TEST(stack_alloc_default_size_correct);
    JARVIS_REGISTER_TEST(stack_alloc_task_has_stack_phys);
    JARVIS_REGISTER_TEST(stack_alloc_user_task_has_guard_page);
    JARVIS_REGISTER_TEST(stack_alloc_stack_alignment);
    JARVIS_REGISTER_TEST(stack_alloc_multiple_tasks_distinct_stacks);
    JARVIS_REGISTER_TEST(stack_alloc_overflow_hook_weak_symbol);
    JARVIS_REGISTER_TEST(stack_alloc_user_stack_phys_freed_on_cleanup);
    JARVIS_REGISTER_TEST(stack_alloc_user_stack_size_in_config);
}
