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

// Runmode: kernel
// Testidea: Verifies Scheduler::needs_switch() returns true when a higher-priority
// READY task exists and current task has lower priority.
// Input: Current task priority=5, create READY task with priority=9.
// Expect: needs_switch() returns true.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_higher_priority, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->base_priority = 5;
    cur->priority = 5;

    auto* high = TaskControlBlock::create([]() {}, 9, 10);
    JARVIS_ASSERT(high != nullptr);
    high->state = TaskState::READY;
    Scheduler::add_task(*high);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == true);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies Scheduler::needs_switch() returns false when current task
// and a READY task share the same priority (round-robin handled by tick).
// Input: Current task priority=5, create READY task with priority=5.
// Expect: needs_switch() returns false.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_equal_priority, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->base_priority = 5;
    cur->priority = 5;

    auto* equal = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(equal != nullptr);
    equal->state = TaskState::READY;
    Scheduler::add_task(*equal);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == false);

    Scheduler::remove_task(*equal);
    equal->cleanup();
    delete equal;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies Scheduler::needs_switch() returns false when a higher-priority
// task exists but is BLOCKED (not runnable).
// Input: Current task priority=5, create BLOCKED task with priority=9.
// Expect: needs_switch() returns false.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_blocked_higher, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->base_priority = 5;
    cur->priority = 5;

    auto* high = TaskControlBlock::create([]() {}, 9, 10);
    JARVIS_ASSERT(high != nullptr);
    high->state = TaskState::BLOCKED;
    Scheduler::add_task(*high);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == false);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies set_preemptible(false) causes needs_switch() to return false
// even with a higher-priority READY task present.
// Input: Current task preemptible=false, create READY task with priority=9.
// Expect: needs_switch() returns false.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_disabled_blocks_switch, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->base_priority = 5;
    cur->priority = 5;

    Scheduler::set_preemptible(false);

    auto* high = TaskControlBlock::create([]() {}, 9, 10);
    JARVIS_ASSERT(high != nullptr);
    high->state = TaskState::READY;
    Scheduler::add_task(*high);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == false);

    Scheduler::set_preemptible(true);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies task with remaining_ticks=0 causes needs_switch() to return
// true; after reload, remaining_ticks equals period_ticks.
// Input: Current task remaining_ticks=0, period_ticks=10.
// Expect: needs_switch() returns true; after on_tick reload, remaining_ticks=10.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_quantum_exhaustion, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->remaining_ticks = 0;
    cur->period_ticks = 10;

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == true);

    // Simulate on_tick to reload
    Scheduler::on_tick();

    JARVIS_ASSERT_EQ(cur->remaining_ticks, 10ULL);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies toggling preemption on/off repeatedly does not crash and
// state remains consistent.
// Input: Loop 100 times: set_preemptible(true), set_preemptible(false).
// Expect: No crash; is_preemptible() reflects each toggle correctly.
// Depends: kernel::task::Scheduler
JARVIS_TEST(preemption_interrupt_enable_disable_cycle, "PRE: none | POST: none") {
    for (uint64_t i = 0; i < 100; ++i) {
        Scheduler::set_preemptible(true);
        JARVIS_ASSERT(Scheduler::is_preemptible() == true);

        Scheduler::set_preemptible(false);
        JARVIS_ASSERT(Scheduler::is_preemptible() == false);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies next_task() never returns the same task when another READY
// task exists at the same priority.
// Input: Current task priority=5, create another READY task priority=5.
// Expect: next_task() returns the other task (not current).
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_task_switch_does_not_switch_to_self, "PRE: none | POST: none") {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->priority = 5;

    auto* other = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(other != nullptr);
    other->state = TaskState::READY;
    Scheduler::add_task(*other);

    auto* next = Scheduler::next_task();
    JARVIS_ASSERT(next != cur);
    JARVIS_ASSERT(next == other);

    Scheduler::remove_task(*other);
    other->cleanup();
    delete other;
    JARVIS_TEST_PASS();
}

void register_preemption_tests() {
    Logger::info("Registering preemption tests");
    JARVIS_REGISTER_TEST(preemption_needs_switch_higher_priority);
    JARVIS_REGISTER_TEST(preemption_needs_switch_equal_priority);
    JARVIS_REGISTER_TEST(preemption_needs_switch_blocked_higher);
    JARVIS_REGISTER_TEST(preemption_disabled_blocks_switch);
    JARVIS_REGISTER_TEST(preemption_quantum_exhaustion);
    JARVIS_REGISTER_TEST(preemption_interrupt_enable_disable_cycle);
    JARVIS_REGISTER_TEST(preemption_task_switch_does_not_switch_to_self);
}