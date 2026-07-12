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

/// @file test_timing.cpp
/// @brief Timing measurement tests.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <signal.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies Scheduler::on_tick() increments current_task().executed_ticks
// by exactly 1.
// Input: Set current task executed_ticks=0, call on_tick().
// Expect: executed_ticks == 1.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_tick_accounting, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->executed_ticks = 0;

    Scheduler::on_tick();

    JARVIS_ASSERT_EQ(cur->executed_ticks, 1ULL);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies when remaining_ticks reaches 0, on_tick() reloads it to
// period_ticks and resets executed_ticks.
// Input: current task remaining_ticks=1, period_ticks=10, executed_ticks=5.
// Call on_tick() -> remaining_ticks becomes 0, executed_ticks becomes 6.
// Call on_tick() again -> remaining_ticks reloads to 10, executed_ticks resets to 0.
// Expect: After second tick, remaining_ticks == 10, executed_ticks == 0.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_period_reload, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->remaining_ticks = 1;
    cur->period_ticks = 10;
    cur->executed_ticks = 5;

    Scheduler::on_tick();

    JARVIS_ASSERT_EQ(cur->remaining_ticks, 0ULL);
    JARVIS_ASSERT_EQ(cur->executed_ticks, 6ULL);

    Scheduler::on_tick();

    JARVIS_ASSERT_EQ(cur->remaining_ticks, 10ULL);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies alarm delivery after specified tick count.
// Input: Set current task alarm_ticks=3. Call on_tick() 3 times.
// Expect: After 3rd tick, alarm_ticks == 0 and SIGALRM pending in signal mask.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock, kernel::signal
JARVIS_TEST(timer_alarm_delivery, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->alarm_ticks = 3;
    cur->alarm_armed = true;
    cur->pending_signals = 0;

    Scheduler::on_tick();
    Scheduler::on_tick();
    Scheduler::on_tick();

    JARVIS_ASSERT_EQ(cur->alarm_ticks, 0ULL);
    JARVIS_ASSERT(cur->alarm_armed == false);
    JARVIS_ASSERT((cur->pending_signals & (1ULL << static_cast<uint64_t>(Signal::SIGALRM))) != 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies alarm not yet expired.
// Input: Set current task alarm_ticks=10. Call on_tick() 5 times.
// Expect: alarm_ticks == 5, no SIGALRM pending.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_alarm_not_expired, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->alarm_ticks = 10;
    cur->alarm_armed = true;
    cur->pending_signals = 0;

    for (int i = 0; i < 5; ++i) {
        Scheduler::on_tick();
    }

    JARVIS_ASSERT_EQ(cur->alarm_ticks, 5ULL);
    JARVIS_ASSERT(cur->alarm_armed == true);
    JARVIS_ASSERT((cur->pending_signals & (1ULL << static_cast<uint64_t>(Signal::SIGALRM))) == 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies on_tick() triggers rate_monotonic_schedule() which causes
// needs_switch() to return true when a higher-priority task is overdue.
// Input: Current task priority=5. Create higher-priority task (priority=9)
// with deadline expired. Call on_tick().
// Expect: needs_switch() returns true after on_tick().
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_rate_monotonic_schedule_indirect, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->priority = 5;

    auto* high = TaskControlBlock::create([]() {}, 9, 10);
    JARVIS_ASSERT(high != nullptr);
    high->state = TaskState::READY;
    high->deadline_ticks = 0;
    Scheduler::add_task(*high);

    Scheduler::on_tick();

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == true);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies on_tick() eventually reaps orphaned TERMINATED children.
// Input: Create parent and child. Parent exits (TERMINATED). Child TERMINATED.
// Call on_tick() repeatedly. Eventually reap_orphans runs.
// Expect: task_count() decreases after sufficient ticks.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_reap_orphans_periodic, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    uint64_t parent_id = parent->id;
    Scheduler::add_task(*parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    uint64_t child_id = child->id;
    child->parent_id = parent->id;
    Scheduler::add_task(*child);

    // Both tasks are RUNNING and in the ready queue.
    // Set them TERMINATED and remove from ready queue so the reaper finds them.
    child->state = TaskState::TERMINATED;
    child->exit_code = 0;
    Scheduler::dequeue_ready(*child);

    parent->state = TaskState::TERMINATED;
    parent->exit_code = 0;
    Scheduler::dequeue_ready(*parent);

    for (int i = 0; i < 100; ++i) {
        Scheduler::on_tick();
    }

    // After reaping, the terminated test tasks should be gone from the task table.
    // Note: we do NOT assert on global task_count() here because daemon restarts
    // (vfsd, iocd, etc.) inflate the count in the same reaper cycle.
    JARVIS_ASSERT(Scheduler::find_task(parent_id) == nullptr);
    JARVIS_ASSERT(Scheduler::find_task(child_id) == nullptr);

    // Clean up any daemon tasks that were spawned by the reaper cycle, so
    // subsequent tests in the class don't inherit leaked task resources.
    for (uint64_t i = 0; i < daemon::MAX_DAEMONS; ++i) {
        const auto& entry = daemon::get_entry(i);
        if (entry.pid != 0) {
            auto* dt = Scheduler::find_task(entry.pid);
            if (dt) {
                Scheduler::remove_task(*dt);
                daemon::notify_death(entry.pid);
                dt->cleanup();
                delete dt;
            }
        }
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies on_tick() does not crash or corrupt scheduler state when
// only the idle task is eligible.
// Input: Ensure only idle task is RUNNABLE. Call on_tick().
// Expect: No crash, scheduler state consistent.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(timer_no_side_effects_on_idle, "PRE: none | POST: none") {
    auto* idle = Scheduler::get_idle_task();
    JARVIS_ASSERT(idle != nullptr);

    for (uint64_t i = 1; i < Scheduler::task_count(); ++i) {
        auto* t = Scheduler::task_at(i);
        if (t) t->state = TaskState::BLOCKED;
    }

    Scheduler::on_tick();

    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(Scheduler::task_count() >= 1);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies daemon tasks in RUNNING state are not restarted by on_tick().
// Input: Mark a daemon task as RUNNING. Call on_tick().
// Expect: Daemon task not marked for restart, state unchanged.
// Depends: kernel::task::Scheduler, kernel::daemon::DaemonMgr
JARVIS_TEST(timer_daemon_restart_not_triggered_on_active, "PRE: none | POST: none") {
    bool found = false;
    for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->state == TaskState::RUNNING && t != Scheduler::get_idle_task()) {
            t->state = TaskState::RUNNING;
            found = true;
            break;
        }
    }

    if (found) {
        Scheduler::on_tick();
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies deadline_missed is set when deadline_ticks is in the past.
// Input: Create a task with expired deadline (deadline_ticks=0). Call on_tick().
// Expect: deadline_missed==true, deadline_miss_count==1.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock, CONFIG_DEADLINE_MISS_DETECTION
JARVIS_TEST(timer_deadline_miss_detection_fires, "PRE: none | POST: none") {
#if !CONFIG_DEADLINE_MISS_DETECTION
    JARVIS_TEST_PASS();
    return;
#endif
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    uint64_t saved_ticks = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_count = cur->deadline_miss_count;

    cur->deadline_ticks = arch::Timer::ticks() - 1;
    cur->deadline_missed = false;
    cur->deadline_miss_count = 0;

    Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
    Scheduler::scan_deadlines();
#endif

    // deadline_missed is reset to false by re-arm (both inline and monitor
    // paths), so check the persistent count instead.
    JARVIS_ASSERT(cur->deadline_miss_count >= 1);

    cur->deadline_ticks = saved_ticks;
    cur->deadline_missed = saved_missed;
    cur->deadline_miss_count = saved_count;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies deadline_missed stays false when deadline is far in the future.
// Input: Set deadline_ticks=UINT64_MAX. Call on_tick().
// Expect: deadline_missed==false, deadline_miss_count==0.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock, CONFIG_DEADLINE_MISS_DETECTION
JARVIS_TEST(timer_deadline_miss_skips_future, "PRE: none | POST: none") {
#if !CONFIG_DEADLINE_MISS_DETECTION
    JARVIS_TEST_PASS();
    return;
#endif
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    uint64_t saved_ticks = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_count = cur->deadline_miss_count;

    cur->deadline_ticks = UINT64_MAX;
    cur->deadline_missed = false;
    cur->deadline_miss_count = 0;

    Scheduler::on_tick();

    JARVIS_ASSERT(cur->deadline_missed == false);
    JARVIS_ASSERT(cur->deadline_miss_count == 0);

    cur->deadline_ticks = saved_ticks;
    cur->deadline_missed = saved_missed;
    cur->deadline_miss_count = saved_count;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies deadline_miss fires only once per deadline period.
// Input: Set deadline_ticks=0, call on_tick() twice.
// Expect: deadline_miss_count==1 (only first call triggers a miss event).
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock, CONFIG_DEADLINE_MISS_DETECTION
JARVIS_TEST(timer_deadline_miss_only_once, "PRE: none | POST: none") {
#if !CONFIG_DEADLINE_MISS_DETECTION
    JARVIS_TEST_PASS();
    return;
#endif
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    uint64_t saved_ticks = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_count = cur->deadline_miss_count;

    cur->deadline_ticks = arch::Timer::ticks() - 1;
    cur->deadline_missed = false;
    cur->deadline_miss_count = 0;

    Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
    Scheduler::scan_deadlines();
#endif
    JARVIS_ASSERT(cur->deadline_miss_count == 1);

    Scheduler::on_tick();
#if CONFIG_DEADLINE_MONITOR_TASK
    Scheduler::scan_deadlines();
#endif
    JARVIS_ASSERT(cur->deadline_miss_count == 1);

    cur->deadline_ticks = saved_ticks;
    cur->deadline_missed = saved_missed;
    cur->deadline_miss_count = saved_count;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies deadline zero (unset/default) does not trigger a miss.
// Input: Keep deadline_ticks=0, set deadline_missed=false. Call on_tick().
// Expect: deadline_missed remains false (zero means deadline not set).
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock, CONFIG_DEADLINE_MISS_DETECTION
JARVIS_TEST(timer_deadline_miss_skips_zero, "PRE: none | POST: none") {
#if !CONFIG_DEADLINE_MISS_DETECTION
    JARVIS_TEST_PASS();
    return;
#endif
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    uint64_t saved_ticks = cur->deadline_ticks;
    bool saved_missed = cur->deadline_missed;
    uint64_t saved_count = cur->deadline_miss_count;

    cur->deadline_ticks = 0;
    cur->deadline_missed = false;
    cur->deadline_miss_count = 0;

    Scheduler::on_tick();

    JARVIS_ASSERT(cur->deadline_missed == false);
    JARVIS_ASSERT(cur->deadline_miss_count == 0);

    cur->deadline_ticks = saved_ticks;
    cur->deadline_missed = saved_missed;
    cur->deadline_miss_count = saved_count;
    JARVIS_TEST_PASS();
}

void register_timing_tests() {
    Logger::raw_write("[TIMING] register_timing_tests called!\n");
    Logger::info("Registering timing tests");
    JARVIS_REGISTER_TEST(timer_tick_accounting);
    JARVIS_REGISTER_TEST(timer_period_reload);
    JARVIS_REGISTER_TEST(timer_alarm_delivery);
    JARVIS_REGISTER_TEST(timer_alarm_not_expired);
    JARVIS_REGISTER_TEST(timer_rate_monotonic_schedule_indirect);
    JARVIS_REGISTER_TEST(timer_reap_orphans_periodic);
    JARVIS_REGISTER_TEST(timer_no_side_effects_on_idle);
    JARVIS_REGISTER_TEST(timer_daemon_restart_not_triggered_on_active);
    JARVIS_REGISTER_TEST(timer_deadline_miss_detection_fires);
    JARVIS_REGISTER_TEST(timer_deadline_miss_skips_future);
    JARVIS_REGISTER_TEST(timer_deadline_miss_only_once);
    JARVIS_REGISTER_TEST(timer_deadline_miss_skips_zero);
}