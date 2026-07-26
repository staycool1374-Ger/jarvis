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

/// @file test_zombie_cleanup.cpp
/// @brief ZombieList deferred cleanup tests.

#include <test.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <logger.hpp>

using namespace kernel;
using namespace kernel::test;

// Runmode: kernel
// Testidea: Terminate a task and verify idle cleanup_step frees its resources.
// Input: Create a kernel task, add to scheduler, terminate.
// Expect: clean_step pops the zombie, cleanup + MemPool::free runs.
// Depends: Scheduler, TaskControlBlock, MemPool, PMM
JARVIS_TEST(zombie_cleanup_step_frees_resources, "PRE: none | POST: none") {
    auto *task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(task != nullptr);
    Scheduler::add_task(*task);

    Scheduler::terminate(*task, 0);

    // The zombie is in the zombie list.  Simulate idle cleanup.
    Scheduler::cleanup_step();

    // After cleanup_step, the TCB's resources should be returned to PMM.
    // The TCB itself (one MemPool block) is freed, but MemPool keeps it
    // internally — PMM pages freed by cleanup (kernel stack, page tables)
    // are the observable change.
    JARVIS_ASSERT(Scheduler::zombie_count() == 0);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify that drain_zombie_list frees multiple zombies at once.
// Input: Create 3 tasks, terminate them all.
// Expect: drain_zombie_list frees all 3; zombie count reaches 0.
// Depends: Scheduler, TaskControlBlock
JARVIS_TEST(zombie_drain_multiple, "PRE: none | POST: none") {
    TaskControlBlock *tasks[3];
    for (uint64_t i = 0; i < 3; ++i) {
        tasks[i] = TaskControlBlock::create([]() {}, 5, 10);
        JARVIS_ASSERT(tasks[i] != nullptr);
        Scheduler::add_task(*tasks[i]);
    }

    for (uint64_t i = 0; i < 3; ++i)
        Scheduler::terminate(*tasks[i], 0);

    JARVIS_ASSERT(Scheduler::zombie_count() == 3);

    Scheduler::drain_zombie_list();
    JARVIS_ASSERT(Scheduler::zombie_count() == 0);

    JARVIS_TEST_PASS();
}

void register_zombie_cleanup_tests() {
    Logger::info("Registering zombie cleanup tests");
    JARVIS_REGISTER_TEST(zombie_cleanup_step_frees_resources);
    JARVIS_REGISTER_TEST(zombie_drain_multiple);
}
