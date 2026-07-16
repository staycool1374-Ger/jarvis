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

/// @file test_deadline_action.cpp
/// @brief Phase 7 (P7a) — Deadline-miss handler ACTION dispatch coverage.
///
///        The handler action is selected at COMPILE TIME by
///        CONFIG_DEADLINE_ACTION (jarvis_config.h). To verify each action
///        (LOG_ONLY / DEMOTE / KILL / NOTIFY_MONITOR) this file is compiled
///        once per action under the config matrix (tools/deadline_matrix.sh).
///        Exactly one test is registered per build — the one matching the
///        compiled action — so the class always contributes a single test
///        and the `all`/per-class expected counts stay stable.
///
///        action == 1 (PANIC) has no in-suite test: the handler halts the
///        kernel, so the matrix treats that build as an expected-fail.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <signal.hpp>

using namespace kernel;

namespace {

/// @brief Create a helper task, park it BLOCKED, and arm a deadline in the
///        past so the next detection scan fires a miss on it (and only it).
///        Callers must CT_ASSERT the returned pointer (CT_ASSERT is only valid
///        inside a TEST_CLASS, not here).
TaskControlBlock *arm_past_deadline_helper() {
    auto *helper = TaskControlBlock::create([]() {}, 10, 10);
    if (helper == nullptr)
        return nullptr;
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);
    Scheduler::dequeue_ready(*helper);
    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*helper);
        helper->state = TaskState::BLOCKED;
    }
    helper->period_ticks = 10;
    helper->deadline_ticks = arch::Timer::ticks() - 1;
    helper->deadline_missed = false;
    helper->deadline_miss_count = 0;
    return helper;
}

/// @brief Run the detection path the way production tests do: on_tick() then
///        scan_deadlines() (the latter only exists under
///        CONFIG_DEADLINE_MONITOR_TASK, which is the default).
void run_detection() {
    arch::IrqGuard guard;
    Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
    Scheduler::scan_deadlines();
#endif
}

/// @brief Push every other live task's deadline far into the future so the
///        detection scan can only fire on `helper`. Under the monitor-task
///        path scan_deadlines() walks ALL tasks; during tests the monitor is
///        suppressed (s_test_active_) so boot tasks do not auto-re-arm and a
///        stray past deadline would otherwise be acted on too (catastrophic
///        for KILL). Field restoration at test teardown reverts this by id.
void neutralize_other_deadlines(TaskControlBlock *helper) {
    for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
        auto *t = Scheduler::task_at(i);
        if (t == nullptr || t->magic != TaskControlBlock::TCB_MAGIC)
            continue;
        if (t == helper)
            continue;
        if (t->state == TaskState::TERMINATED)
            continue;
        t->deadline_ticks = UINT64_MAX - 1000000000ULL;
    }
}

} // namespace

#if CONFIG_DEADLINE_ACTION == 0
// Runmode: kernel
// Testidea: LOG_ONLY (action=0, default build) records the miss and leaves
// the task alive with unchanged priority.
// Input: BLOCKED helper with past deadline; detection run.
// Expect: deadline_miss_count>=1, state != TERMINATED, priority unchanged.
TEST_CLASS(DeadlineActionLogOnly) {
    auto *helper = arm_past_deadline_helper();
    CT_ASSERT(helper != nullptr);
    uint64_t saved_prio = helper->priority;
    neutralize_other_deadlines(helper);
    run_detection();
    CT_ASSERT(helper->deadline_miss_count >= 1);
    CT_ASSERT(helper->state != TaskState::TERMINATED);
    CT_ASSERT(helper->priority == saved_prio);
    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};
#endif

#if CONFIG_DEADLINE_ACTION == 2
// Runmode: kernel
// Testidea: DEMOTE (action=2) halves the offending task's priority (floored
// at 1) on miss.
// Input: BLOCKED helper priority 10, past deadline; detection run.
// Expect: deadline_miss_count>=1, priority == 10>>1 == 5, priority >= 1.
TEST_CLASS(DeadlineActionDemote) {
    auto *helper = arm_past_deadline_helper();
    CT_ASSERT(helper != nullptr);
    uint64_t saved_prio = helper->priority;
    neutralize_other_deadlines(helper);
    run_detection();
    CT_ASSERT(helper->deadline_miss_count >= 1);
    CT_ASSERT(helper->priority == (saved_prio >> 1));
    CT_ASSERT(helper->priority >= 1);
    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};
#endif

#if CONFIG_DEADLINE_ACTION == 3
// Runmode: kernel
// Testidea: KILL (action=3) marks the task TERMINATED and defers cleanup.
// The deferred kill is flushed by process_deferred_kills() (the same call
// on_tick() makes after returning) and must be leak-free (framework
// snapshot/restore checks ResourceTracker).
// Input: BLOCKED helper, past deadline; detection run; flush deferred kills.
// Expect: deadline_miss_count>=1, state == TERMINATED. Helper is freed by
//         the flush — do NOT dereference afterwards.
TEST_CLASS(DeadlineActionKill) {
    auto *helper = arm_past_deadline_helper();
    CT_ASSERT(helper != nullptr);
    neutralize_other_deadlines(helper);
    run_detection();
    CT_ASSERT(helper->deadline_miss_count >= 1);
    CT_ASSERT(helper->state == TaskState::TERMINATED);
    // Flush the deferred kill list (mirrors on_tick() post-lock path).
    Scheduler::process_deferred_kills();
    // helper is now freed; leak verified by test isolation restore.
};
#endif

#if CONFIG_DEADLINE_ACTION == 4
// Runmode: kernel
// Testidea: NOTIFY_MONITOR (action=4) delivers SIGUSR1 to the monitor task.
// The test-only hook g_test_deadline_monitor_pid is pointed at the live
// [deadline-mon] task so the compile-time CONFIG_DEADLINE_MONITOR_PID (0 by
// default) is bypassed.
// Input: BLOCKED helper, past deadline; hook set to monitor id; detection run.
// Expect: deadline_miss_count>=1, monitor pending_signals has SIGUSR1.
TEST_CLASS(DeadlineActionNotifyProbe) {
    auto *helper = arm_past_deadline_helper();
    CT_ASSERT(helper != nullptr);
    auto *mon = Scheduler::get_monitor_task();
    CT_ASSERT(mon != nullptr);
    g_test_deadline_monitor_pid = mon->id;
    uint64_t before = mon->pending_signals;
    (void)before;
    neutralize_other_deadlines(helper);
    run_detection();
    CT_ASSERT(helper->deadline_miss_count >= 1);
    CT_ASSERT((mon->pending_signals &
               (1ULL << static_cast<uint64_t>(Signal::SIGUSR1))) != 0);
    g_test_deadline_monitor_pid = 0;
    Scheduler::set_task_ready(*helper);
    Scheduler::remove_task(*helper);
    helper->cleanup();
    delete helper;
};
#endif

#if CONFIG_DEADLINE_ACTION == 1
// Runmode: kernel
// Testidea: PANIC (action=1) must halt the kernel when a deadline miss fires.
// This test only compiles under CONFIG_DEADLINE_ACTION==1. It triggers a miss
// and expects the handler to call panic() BEFORE the test completes — the
// config matrix treats the presence of the "action=PANIC" message as the
// success signal (the kernel intentionally does not return here).
// Input: BLOCKED helper, past deadline; detection run.
// Expect: kernel panics with [DMD] ... action=PANIC (verified by the matrix,
//         not by an in-test assertion).
TEST_CLASS(DeadlineActionPanics) {
    auto *helper = arm_past_deadline_helper();
    CT_ASSERT(helper != nullptr);
    neutralize_other_deadlines(helper);
    run_detection();
    // Reached only if PANIC did NOT fire — that is a failure of the action.
    CT_ASSERT(false);
};
#endif

void register_deadline_action_tests() {
#if CONFIG_DEADLINE_ACTION == 0
    REGISTER_CLASS(DeadlineActionLogOnly);
#elif CONFIG_DEADLINE_ACTION == 1
    REGISTER_CLASS(DeadlineActionPanics);
#elif CONFIG_DEADLINE_ACTION == 2
    REGISTER_CLASS(DeadlineActionDemote);
#elif CONFIG_DEADLINE_ACTION == 3
    REGISTER_CLASS(DeadlineActionKill);
#elif CONFIG_DEADLINE_ACTION == 4
    REGISTER_CLASS(DeadlineActionNotifyProbe);
#endif
}
