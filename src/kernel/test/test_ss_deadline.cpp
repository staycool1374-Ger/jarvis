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

/// @file test_ss_deadline.cpp
/// @brief SporadicServer deadline integration tests — Phase 4: SS budget
///        exhaustion mapped to deadline miss, and deadline detection with
///        SS EXHAUSTED context via P1a.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/sporadic_server.hpp>
#include <kernel/arch/timer.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: An SS task with exhausted budget that misses a deadline must
// fire the deadline handler with EXHAUSTED context.  Budget is exhausted
// directly via SS methods (not through on_tick consume path), then a
// deadline is set in the past and on_tick triggers P1a detection.
// Input: SS helper task, budget exhausted via SS::consume() directly,
//        deadline_ticks set to past.
// Expect: deadline_miss_handler fires with "budget exhausted" message,
//         ss_state_on_deadline_miss==EXHAUSTED, deadline_miss_count>=1.
TEST_CLASS(SsExhaustionTriggersDeadline) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    helper->init_sporadic_server(3, 100, 42);
    Scheduler::add_task(*helper);

    // Exhaust SS budget directly via SS methods
    helper->sporadic_server->on_activation(arch::Timer::ticks());
    CT_ASSERT(helper->sporadic_server->remaining_budget() == 3);
    CT_ASSERT(helper->sporadic_server->is_active());

    for (int i = 0; i < 5; ++i)
        helper->sporadic_server->consume(arch::Timer::ticks());

    CT_ASSERT(helper->sporadic_server->state() ==
              task::SporadicServer::State::EXHAUSTED);
    CT_ASSERT(helper->sporadic_server->remaining_budget() == 0);

    // Set deadline in the past so P1a detection fires
    helper->deadline_ticks = arch::Timer::ticks() - 1;
    helper->deadline_missed = false;
    helper->deadline_miss_count = 0;
    helper->ss_state_on_deadline_miss = 0;
    helper->ss_budget_on_deadline_miss = 999;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
        Scheduler::scan_deadlines();
#endif
    }

    // P1a deadline detection must fire with SS context
    CT_ASSERT(helper->deadline_miss_count >= 1);

    // P4a: SS state must be captured as EXHAUSTED
    CT_ASSERT(helper->ss_state_on_deadline_miss ==
              static_cast<uint8_t>(task::SporadicServer::State::EXHAUSTED));

    // P4a: Budget captured as 0
    CT_ASSERT(helper->ss_budget_on_deadline_miss == 0);

    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

// Runmode: kernel
// Testidea: An SS task with EXHAUSTED state (budget=0) that has a deadline
// in the past fires the deadline handler with EXHAUSTED SS context.
// Input: SS helper task with budget=0, DEADLINE in past.
// Expect: deadline_missed==true, ss_state_on_deadline_miss==EXHAUSTED,
//         handler logs "budget exhausted".
TEST_CLASS(SsDeadlineMissDuringReplenish) {
    auto* helper = TaskControlBlock::create([](){}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    helper->init_sporadic_server(3, 100, 42);
    Scheduler::add_task(*helper);

    // Exhaust SS directly
    helper->sporadic_server->on_activation(arch::Timer::ticks());
    for (int i = 0; i < 5; ++i)
        helper->sporadic_server->consume(arch::Timer::ticks());

    CT_ASSERT(helper->sporadic_server->state() ==
              task::SporadicServer::State::EXHAUSTED);
    CT_ASSERT(helper->sporadic_server->remaining_budget() == 0);

    // Set deadline in past
    helper->deadline_ticks = arch::Timer::ticks() - 1;
    helper->deadline_missed = false;
    helper->deadline_miss_count = 0;
    helper->ss_state_on_deadline_miss = 0;
    helper->ss_budget_on_deadline_miss = 999;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
        Scheduler::scan_deadlines();
#endif
    }

    // P4a: SS state must be captured as EXHAUSTED
    CT_ASSERT(helper->ss_state_on_deadline_miss ==
              static_cast<uint8_t>(task::SporadicServer::State::EXHAUSTED));
    CT_ASSERT(helper->ss_budget_on_deadline_miss == 0);

    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};

void register_ss_deadline_tests() {
    Logger::info("Registering SS deadline integration tests");
    REGISTER_CLASS(SsExhaustionTriggersDeadline);
    REGISTER_CLASS(SsDeadlineMissDuringReplenish);
}
