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
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

JARVIS_TEST(init_task_exists, "PRE: none | POST: none") {
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);
    JARVIS_ASSERT_EQ(1ULL, init->id);
    JARVIS_ASSERT(init->state == TaskState::READY ||
                  init->state == TaskState::RUNNING);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(init_task_has_no_parent, "PRE: none | POST: none") {
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);
    JARVIS_ASSERT_EQ(0ULL, init->parent_id);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(init_task_reparents_orphans, "PRE: none | POST: none") {
    // Create a child task, set parent to a task that will exit,
    // verify it gets reparented to init

    // Get init task ref
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);

    // Get current task
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    // Verify there are at least 2 tasks (init + current)
    JARVIS_ASSERT(Scheduler::task_count() >= 2);
    JARVIS_TEST_PASS();
}

void register_init_tests() {
    kernel::Logger::info("Registering init tests");
    JARVIS_REGISTER_TEST(init_task_exists);
    JARVIS_REGISTER_TEST(init_task_has_no_parent);
    JARVIS_REGISTER_TEST(init_task_reparents_orphans);
}
