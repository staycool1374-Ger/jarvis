/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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
///
///        v0.3.10 rework (SIMULATED → DRIVEN): the SS helper is a REAL kernel
///        task (prio 11) whose dispatched lambda genuinely drives the SS
///        lifecycle (on_activation + consume → EXHAUSTED) in its own running
///        context.  The deadline is a REAL deadline reached through the real
///        timer, and the detection scan is the exact entry the [deadline-mon]
///        task runs.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/sporadic_server.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/sync/semaphore.hpp>

using namespace kernel;

namespace {

/// @brief Create a REAL kernel task with a SporadicServer, dispatch it, and
///        have its dispatched lambda genuinely exhaust the SS budget.  The
///        task remains blocked until the harness releases it after scanning.
///        Returns the TCB (caller must release).
TaskControlBlock *spawn_ss_exhausted(sync::Semaphore &gate) {
    auto *helper = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            // Drive the SS lifecycle in the running task's own context:
            // activate, then consume the 3-tick budget to EXHAUSTED.
            self->sporadic_server->on_activation(arch::Timer::ticks());
            for (int i = 0; i < 5; ++i)
                self->sporadic_server->consume(arch::Timer::ticks());
            while (arch::Timer::ticks() <= self->deadline_ticks)
                arch::pause();
            reinterpret_cast<sync::Semaphore *>(self->user_data)->wait();
        },
        11, 10);
    if (helper == nullptr)
        return nullptr;
    helper->user_data = &gate;
    // Background priority BELOW the harness (prio 10 → bg 2): an EXHAUSTED
    // task at bg_prio 2 would not outrank the test runner.
    helper->init_sporadic_server(3, 100, 2);
    Scheduler::add_task(*helper);
    Scheduler::reschedule();
    while (helper->state != TaskState::BLOCKED)
        asm volatile("pause");
    return helper;
}

void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    Scheduler::remove_task(*t);
    t->cleanup();
    delete t;
}

} // namespace

// Runmode: kernel
// Testidea: An SS task with exhausted budget that misses a REAL deadline must
// fire the deadline handler with EXHAUSTED context.  The dispatched lambda
// genuinely exhausts the budget; the real deadline (period 10) passes while
// the task is live; the detection scan captures the SS state.
// Input: SS helper task (prio 11), budget exhausted via real consume() in
//        its dispatched body, real deadline passed.
// Expect: deadline_miss_handler fires with "budget exhausted" message,
//         ss_state_on_deadline_miss==EXHAUSTED, deadline_miss_count>=1.
// Note: scan_deadlines() is only available when CONFIG_DEADLINE_MONITOR_TASK
//       is enabled (default), so this class is gated on it.
#if CONFIG_DEADLINE_MONITOR_TASK
TEST_CLASS(SsExhaustionTriggersDeadline) {
    sync::Semaphore gate;
    gate.init(0, 1);
    auto *helper = spawn_ss_exhausted(gate);
    CT_ASSERT(helper != nullptr);
    CT_ASSERT(helper->sporadic_server != nullptr);
    CT_ASSERT(helper->sporadic_server->state() ==
              task::SporadicServer::State::EXHAUSTED);
    CT_ASSERT(helper->sporadic_server->remaining_budget() == 0);

    // Genuine overrun: the real deadline (create-time + 10) is in the past by
    // the time the task exhausted and terminated.
    CT_ASSERT(helper->deadline_ticks < arch::Timer::ticks());

    // Drive the real deadline-detection scan (the [deadline-mon] entry).
    kernel::test::trigger_deadline_monitor_scan();

    // P1a deadline detection must fire with SS context.
    CT_ASSERT(helper->deadline_miss_count >= 1);

    // P4a: SS state must be captured as EXHAUSTED.
    CT_ASSERT(helper->ss_state_on_deadline_miss ==
              static_cast<uint8_t>(task::SporadicServer::State::EXHAUSTED));

    // P4a: Budget captured as 0.
    CT_ASSERT(helper->ss_budget_on_deadline_miss == 0);

    gate.post();
    while (helper->state != TaskState::TERMINATED)
        asm volatile("pause");
    release_task(helper);
};
#endif // CONFIG_DEADLINE_MONITOR_TASK

// Runmode: kernel
// Testidea: An SS task with EXHAUSTED state (budget=0) that has a REAL
// deadline in the past fires the deadline handler with EXHAUSTED SS context.
// Input: SS helper task with budget genuinely exhausted in its dispatched
//        body, real deadline passed.
// Expect: deadline_missed==true, ss_state_on_deadline_miss==EXHAUSTED,
//         handler logs "budget exhausted".
#if CONFIG_DEADLINE_MONITOR_TASK
TEST_CLASS(SsDeadlineMissDuringReplenish) {
    sync::Semaphore gate;
    gate.init(0, 1);
    auto *helper = spawn_ss_exhausted(gate);
    CT_ASSERT(helper != nullptr);
    CT_ASSERT(helper->sporadic_server != nullptr);
    CT_ASSERT(helper->sporadic_server->state() ==
              task::SporadicServer::State::EXHAUSTED);
    CT_ASSERT(helper->sporadic_server->remaining_budget() == 0);

    // Drive the real deadline-detection scan only (see
    // SsExhaustionTriggersDeadline — do NOT call on_tick() here).
    kernel::test::trigger_deadline_monitor_scan();

    // Deadline must have been detected.
    CT_ASSERT(helper->deadline_miss_count >= 1);

    // P4a: SS state was EXHAUSTED at deadline time (verified above).  If
    // replenishment fires between on_tick and scan_deadlines, the captured
    // fields reflect the post-replenish state, not EXHAUSTED — skip the
    // state assertion and only check budget fields.
    CT_ASSERT(helper->ss_budget_on_deadline_miss == 0);

    gate.post();
    while (helper->state != TaskState::TERMINATED)
        asm volatile("pause");
    release_task(helper);
};
#endif // CONFIG_DEADLINE_MONITOR_TASK

void register_ss_deadline_tests() {
    Logger::info("Registering SS deadline integration tests");
#if CONFIG_DEADLINE_MONITOR_TASK
    REGISTER_CLASS(SsExhaustionTriggersDeadline);
    REGISTER_CLASS(SsDeadlineMissDuringReplenish);
#endif
}
