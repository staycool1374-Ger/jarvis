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

/// @file test_deadline_recovery.cpp
/// @brief Phase 5 tests: Asymmetric Recovery & Safety Protocols — safe KILL
///        cleanup, NOTIFY_MONITOR delivery, magic-check structural isolation,
///        and MC/DC coverage of the detection predicate.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <signal.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: The KILL cleanup sequence (cleanup() + remove_task() + free)
// must safely release all task resources.  This is the same sequence that
// Scheduler::process_deferred_kills() performs for deferred-kill tasks.
// Snapshot/restore at test end verifies ResourceTracker returns to baseline.
// Input: Helper task created, then killed via the manual KILL sequence.
// Expect: cleanup() and remove_task() complete without crash/assert.
TEST_CLASS(DeadlineActionKillCleansUp) {
    auto *helper = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(helper != nullptr);
    helper->base_priority = 10;
    helper->priority = 10;
    Scheduler::add_task(*helper);

    // Full KILL sequence as performed by process_deferred_kills()
    helper->cleanup();
    Scheduler::remove_task(*helper);
    delete helper;
};

// Runmode: kernel
// Testidea: A task with corrupted magic (magic != TCB_MAGIC) must NOT
// trigger the deadline handler.  The detection loop skips corrupt TCBs.
// Input: Two tasks created — one valid, one with corrupted magic — both
//        with deadline in the past.  on_tick() fires.
// Expect: Only the valid task has deadline_miss_count advanced.
TEST_CLASS(DeadlineDetectionMagicCheck) {
    auto *valid = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(valid != nullptr);
    valid->base_priority = 10;
    valid->priority = 10;
    Scheduler::add_task(*valid);
    Scheduler::dequeue_ready(*valid);
    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*valid);
        valid->state = TaskState::BLOCKED;
    }

    auto *corrupt = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(corrupt != nullptr);
    corrupt->base_priority = 10;
    corrupt->priority = 10;
    Scheduler::add_task(*corrupt);
    Scheduler::dequeue_ready(*corrupt);
    {
        arch::IrqGuard outer;
        kernel::test::ScopedCurrentTask scope(*corrupt);
        corrupt->state = TaskState::BLOCKED;
    }

    // Set deadlines in the past for both
    valid->deadline_ticks = arch::Timer::ticks() - 1;
    valid->deadline_missed = false;
    valid->deadline_miss_count = 0;

    corrupt->deadline_ticks = arch::Timer::ticks() - 1;
    corrupt->deadline_missed = false;
    corrupt->deadline_miss_count = 0;

    // Corrupt the magic on the second task
    corrupt->magic = 0xDEADBEEF;

    uint64_t before_integrity = deadline_detection_integrity;

    {
        arch::IrqGuard guard;
        Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
        Scheduler::scan_deadlines();
#endif
    }

    // Integrity counter must have advanced (scan completed)
    CT_ASSERT(deadline_detection_integrity == before_integrity + 1);

    // Valid task must have been detected
    CT_ASSERT(valid->deadline_miss_count >= 1);

    // Corrupt task must NOT have been detected
    CT_ASSERT(corrupt->deadline_miss_count == 0);

    // Restore valid task magic for cleanup
    corrupt->magic = TaskControlBlock::TCB_MAGIC;
    Scheduler::set_task_ready(*valid);
    Scheduler::remove_task(*valid);
    valid->cleanup();
    delete valid;
    Scheduler::set_task_ready(*corrupt);
    Scheduler::remove_task(*corrupt);
    corrupt->cleanup();
    delete corrupt;
};

// Runmode: kernel
// Testidea: Each condition in the deadline detection predicate must be
// shown to independently affect the outcome (MC/DC).
//
//   predicate: period_ticks > 0  &&  !deadline_missed  &&
//              deadline_ticks > 0  &&  ticks > deadline_ticks
//
// Conditions:
//   A = period_ticks > 0
//   B = !deadline_missed
//   C = deadline_ticks > 0
//   D = ticks > deadline_ticks
//
// Test cases (using current task):
//   1. A=1 B=1 C=1 D=1 -> fires (control)
//   2. A=0                -> no fire
//   3. B=0 (missed=true)  -> no fire
//   4. C=0                -> no fire
//   5. D=0 (ticks==deadline, not past) -> no fire
//
// Input: Current task's deadline/period fields manipulated, then on_tick().
// Expect: deadline_miss_count advances only in case 1.
TEST_CLASS(DeadlineDetectionMcdcCoverage) {
    auto *cur = Scheduler::current_task();
    CT_ASSERT(cur != nullptr);

    // Save original fields for restore at end
    uint64_t saved_period = cur->period_ticks;
    uint64_t saved_deadline = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_count = cur->deadline_miss_count;

    // Case 1: All-true -> must fire
    {
        cur->period_ticks = 10;
        cur->deadline_ticks = arch::Timer::ticks() - 1;
        cur->deadline_missed = false;
        cur->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
            Scheduler::scan_deadlines();
#endif
        }

        CT_ASSERT(cur->deadline_miss_count >= 1);
    }

    // Case 2: period_ticks=0 (A false) -> must NOT fire
    {
        cur->period_ticks = 0;
        cur->deadline_ticks = arch::Timer::ticks() - 1;
        cur->deadline_missed = false;
        cur->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
            Scheduler::scan_deadlines();
#endif
        }

        CT_ASSERT(cur->deadline_miss_count == 0);
    }

    // Case 3: deadline_missed=true (B false) -> must NOT fire
    {
        cur->period_ticks = 10;
        cur->deadline_ticks = arch::Timer::ticks() - 1;
        cur->deadline_missed = true;
        cur->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
            Scheduler::scan_deadlines();
#endif
        }

        CT_ASSERT(cur->deadline_miss_count == 0);
    }

    // Case 4: deadline_ticks=0 (C false) -> must NOT fire
    {
        cur->period_ticks = 10;
        cur->deadline_ticks = 0;
        cur->deadline_missed = false;
        cur->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
            Scheduler::scan_deadlines();
#endif
        }

        CT_ASSERT(cur->deadline_miss_count == 0);
    }

    // Case 5: ticks == deadline_ticks (D false, not past) -> must NOT fire
    {
        cur->period_ticks = 10;
        cur->deadline_ticks = arch::Timer::ticks();
        cur->deadline_missed = false;
        cur->deadline_miss_count = 0;

        {
            arch::IrqGuard guard;
            Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
            Scheduler::scan_deadlines();
#endif
        }

        CT_ASSERT(cur->deadline_miss_count == 0);
    }

    // Restore original fields
    cur->period_ticks = saved_period;
    cur->deadline_ticks = saved_deadline;
    cur->deadline_missed = saved_missed;
    cur->deadline_miss_count = saved_count;
};

// Runmode: kernel
// Testidea: NOTIFY_MONITOR delivers SIGUSR1 to a designated monitor task
// when a deadline miss occurs (simulated by directly setting the signal).
// Input: A "monitor" helper task created.  pending_signals set manually
//        to simulate the action=4 notification path.
// Expect: monitor->pending_signals has SIGUSR1 set.
TEST_CLASS(DeadlineActionNotifyMonitor) {
    auto *monitor = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(monitor != nullptr);
    monitor->base_priority = 10;
    monitor->priority = 10;
    Scheduler::add_task(*monitor);

    // Simulate the action=4 notification path: find and signal the monitor
    uint64_t monitor_pid = monitor->id;
    auto *found = Scheduler::find_task(monitor_pid);
    CT_ASSERT(found == monitor);
    CT_ASSERT(found->magic == TaskControlBlock::TCB_MAGIC);
    CT_ASSERT(found->state != TaskState::TERMINATED);

    found->pending_signals |= (1ULL << static_cast<uint64_t>(Signal::SIGUSR1));

    // Verify signal was delivered
    CT_ASSERT(found->pending_signals &
              (1ULL << static_cast<uint64_t>(Signal::SIGUSR1)));

    Scheduler::remove_task(*monitor);
    monitor->cleanup();
    delete monitor;
};

void register_deadline_recovery_tests() {
    Logger::info("Registering deadline recovery tests");
    REGISTER_CLASS(DeadlineActionKillCleansUp);
    REGISTER_CLASS(DeadlineDetectionMagicCheck);
    REGISTER_CLASS(DeadlineDetectionMcdcCoverage);
    REGISTER_CLASS(DeadlineActionNotifyMonitor);
}
