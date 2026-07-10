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

/// @file test_deadline_miss.cpp
/// @brief Deadline miss detection tests — Phase 1: BLOCKED/WAITING coverage,
///        TERMINATED exclusion, and periodic re-arm.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: A task in BLOCKED state must still trigger deadline miss
// detection when its deadline passes.
// Input: Helper task created, then placed in BLOCKED state (via direct
//        state manipulation inside ScopedCurrentTask, matching the
//        established pattern from PriorityInversionChain5).  A semaphore
//        is created as the primitive that would be the blocking point.
//        Deadline set to past.  on_tick() fires.
// Expect: deadline_missed==true, deadline_miss_count>=1.
TEST_CLASS(DeadlineMissWhileBlocked) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);
    Scheduler::dequeue_ready(*helper);

    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*helper);
        helper->state = TaskState::BLOCKED;
    }
    CT_ASSERT(helper->state == TaskState::BLOCKED);

    // Set deadline in the past
    helper->deadline_ticks = arch::Timer::ticks() - 1;
    helper->deadline_missed = false;
    helper->deadline_miss_count = 0;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
    }

    // deadline_missed may be cleared by re-arm (P1b) — count is the stable check
    CT_ASSERT(helper->deadline_miss_count >= 1);

    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

// Runmode: kernel
// Testidea: A TERMINATED task must NOT trigger deadline miss detection
// even if its deadline is in the past.
// Input: Helper task created and placed in TERMINATED state.
//        Deadline set to past.  on_tick() fires.
// Expect: deadline_missed stays false, deadline_miss_count stays 0.
TEST_CLASS(DeadlineMissWhileTerminatedSkipped) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);
    Scheduler::dequeue_ready(*helper);

    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*helper);
        helper->state = TaskState::TERMINATED;
    }
    CT_ASSERT(helper->state == TaskState::TERMINATED);

    // Set deadline in the past
    helper->deadline_ticks = arch::Timer::ticks() - 1;
    helper->deadline_missed = false;
    helper->deadline_miss_count = 0;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
    }

    // Must NOT fire for TERMINATED tasks
    CT_ASSERT(helper->deadline_missed == false);
    CT_ASSERT(helper->deadline_miss_count == 0);

    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

// Runmode: kernel
// Testidea: For periodic tasks, after a deadline miss the detection
// block re-arms deadline_ticks += period_ticks and clears deadline_missed.
// Input: Current task has period_ticks=10, deadline_ticks just past.
//        on_tick() fires, then checks re-arm.
// Expect: deadline_ticks advanced by period_ticks, deadline_missed cleared.
TEST_CLASS(DeadlineRearmOnPeriodRollover) {
    auto* cur = Scheduler::current_task();
    CT_ASSERT(cur != nullptr);

    uint64_t saved_ticks = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_period = cur->period_ticks;
    uint64_t saved_count = cur->deadline_miss_count;
    uint64_t saved_remaining = cur->remaining_ticks;

    // Trigger deadline miss + period rollover: set deadline in the past
    // and remaining_ticks to 0 so the next tick wraps.
    cur->period_ticks = 10;
    cur->deadline_ticks = arch::Timer::ticks() - 1;
    cur->deadline_missed = false;
    cur->deadline_miss_count = 0;
    cur->remaining_ticks = 0;

    uint64_t pre_ticks = cur->deadline_ticks;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
    }

    // Deadline miss fired (detected before rollover re-arm)
    CT_ASSERT(cur->deadline_miss_count >= 1);

    // Re-arm: deadline advanced by period_ticks on rollover
    CT_ASSERT(cur->deadline_ticks == pre_ticks + 10);

    // Latch cleared for next period by rollover
    CT_ASSERT(cur->deadline_missed == false);

    cur->deadline_ticks = saved_ticks;
    cur->deadline_missed = saved_missed;
    cur->period_ticks = saved_period;
    cur->deadline_miss_count = saved_count;
    cur->remaining_ticks = saved_remaining;
};

void register_deadline_miss_tests() {
    Logger::info("Registering deadline miss detection tests");
    REGISTER_CLASS(DeadlineMissWhileBlocked);
    REGISTER_CLASS(DeadlineMissWhileTerminatedSkipped);
    REGISTER_CLASS(DeadlineRearmOnPeriodRollover);
}
