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

/// @file test_wcet_overrun.cpp
/// @brief WCET overrun detection tests — Phase 3: distinguish execution-
///        time overrun from deadline miss.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: A task with wcet_ticks > 0 that exceeds its WCET must fire
// the WCET overrun handler, but NOT the deadline handler.
// Input: Helper task created, placed in RUNNING state via ScopedCurrentTask,
//        wcet_ticks=10, executed_ticks=11 (exceeded), no deadline set.
// Expect: wcet_overrun_fired==true, deadline_missed==false.
TEST_CLASS(WcetOverrunDetectionFires) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);
    Scheduler::dequeue_ready(*helper);

    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*helper);
        helper->state = TaskState::RUNNING;

        // Set WCET low and executed high so overrun is detected
        helper->wcet_ticks = 10;
        helper->executed_ticks = 11;
        helper->wcet_overrun_fired = false;

        // No deadline set — ensure it won't fire
        helper->deadline_ticks = 0;
        helper->deadline_missed = false;
        helper->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
        }

        CT_ASSERT(helper->wcet_overrun_fired == true);
        CT_ASSERT(helper->deadline_missed == false);
        CT_ASSERT(helper->deadline_miss_count == 0);
    }

    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

// Runmode: kernel
// Testidea: A task that meets WCET but misses deadline must fire the
// deadline handler but NOT the WCET overrun handler.
// Input: Helper task created, placed in RUNNING state, wcet_ticks=100
//        (within budget), executed_ticks=5, deadline_ticks in past.
// Expect: deadline_miss_count>=1, wcet_overrun_fired==false.
TEST_CLASS(DeadlineMissWithinWcet) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);
    Scheduler::dequeue_ready(*helper);

    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*helper);
        helper->state = TaskState::RUNNING;

        // WCET is high — within budget
        helper->wcet_ticks = 100;
        helper->executed_ticks = 5;
        helper->wcet_overrun_fired = false;

        // Deadline is in the past
        helper->deadline_ticks = arch::Timer::ticks() - 1;
        helper->deadline_missed = false;
        helper->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
        }

        // Deadline must fire
        CT_ASSERT(helper->deadline_miss_count >= 1);

        // WCET must NOT fire (budget not exceeded)
        CT_ASSERT(helper->wcet_overrun_fired == false);
    }

    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

void register_wcet_overrun_tests() {
    Logger::info("Registering WCET overrun detection tests");
    REGISTER_CLASS(WcetOverrunDetectionFires);
    REGISTER_CLASS(DeadlineMissWithinWcet);
}
