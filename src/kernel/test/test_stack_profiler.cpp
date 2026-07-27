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
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(stack_profiler_task_has_valid_stack, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT(t->kernel_stack != nullptr);
    JARVIS_ASSERT(t->kernel_stack_top > 0);
    JARVIS_ASSERT(reinterpret_cast<uint64_t>(t->kernel_stack) <
                  t->kernel_stack_top);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_profiler_stack_usage_bounded, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    uint64_t stack_base = reinterpret_cast<uint64_t>(t->kernel_stack);
    uint64_t stack_size = t->kernel_stack_top - stack_base;
    JARVIS_ASSERT_FMT(stack_size >= CONFIG_MIN_STACK_SIZE,
                      "Stack size %lu < MIN_STACK_SIZE %lu", stack_size,
                      (uint64_t)CONFIG_MIN_STACK_SIZE);
    JARVIS_ASSERT_FMT(stack_size <= CONFIG_STACK_SIZE,
                      "Stack size %lu > CONFIG_STACK_SIZE %lu", stack_size,
                      (uint64_t)CONFIG_STACK_SIZE);
    JARVIS_ASSERT(stack_size % arch::PAGE_SIZE == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_profiler_context_rsp_in_range, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    uint64_t stack_base = reinterpret_cast<uint64_t>(t->kernel_stack);
    JARVIS_ASSERT(t->context.rsp >= stack_base && t->context.rsp < t->kernel_stack_top);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_profiler_resets_on_cleanup, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT(t->kernel_stack != nullptr);
    JARVIS_ASSERT(t->kernel_stack_top > 0);
    t->cleanup();
    JARVIS_ASSERT(t->kernel_stack == nullptr);
    JARVIS_ASSERT(t->kernel_stack_top == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_profiler_current_task_stack_valid,
            "PRE: none | POST: none") {
    auto *cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->kernel_stack != nullptr);
    JARVIS_ASSERT(cur->kernel_stack_top > 0);
    uint64_t stack_base = reinterpret_cast<uint64_t>(cur->kernel_stack);
    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    JARVIS_ASSERT(rsp >= stack_base);
    JARVIS_ASSERT(rsp < cur->kernel_stack_top);
    uint64_t cur_stack_size = cur->kernel_stack_top - stack_base;
    JARVIS_ASSERT_FMT(cur_stack_size >= CONFIG_MIN_STACK_SIZE,
                      "Current task stack %lu < MIN %lu",
                      cur_stack_size, (uint64_t)CONFIG_MIN_STACK_SIZE);
    JARVIS_ASSERT_FMT(cur_stack_size <= CONFIG_STACK_SIZE,
                      "Current task stack %lu > MAX %lu",
                      cur_stack_size, (uint64_t)CONFIG_STACK_SIZE);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(stack_profiler_user_task_stack_size, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create_user([]() {}, 5, 10, 64_KiB);
    JARVIS_ASSERT(t != nullptr);
    auto cleanup = ScopeGuard([&]() {
        t->cleanup();
        delete t;
    });
    JARVIS_ASSERT(t->user_stack_size_ == 64_KiB);
    JARVIS_ASSERT(t->user_stack_ != 0);
    JARVIS_TEST_PASS();
}

void register_stack_profiler_tests() {
    Logger::info("Registering stack profiler tests");
    JARVIS_REGISTER_TEST(stack_profiler_task_has_valid_stack);
    JARVIS_REGISTER_TEST(stack_profiler_stack_usage_bounded);
    JARVIS_REGISTER_TEST(stack_profiler_context_rsp_in_range);
    JARVIS_REGISTER_TEST(stack_profiler_resets_on_cleanup);
    JARVIS_REGISTER_TEST(stack_profiler_current_task_stack_valid);
    JARVIS_REGISTER_TEST(stack_profiler_user_task_stack_size);
}
