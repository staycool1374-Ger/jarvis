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

/// @file test_preemption.cpp
/// @brief Kernel preemption tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): the needs_switch() predicate is
/// exercised with REAL tasks at real priorities (relative to the harness at
/// prio 10) and the BLOCKED state is reached via a real blocking operation —
/// never via direct `task->state` or `cur->priority` writes.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/semaphore.hpp>

using namespace kernel;

namespace {
void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    Scheduler::remove_task(*t);
    t->cleanup();
    delete t;
}
} // namespace

// Runmode: kernel
// Testidea: needs_switch() returns true when a higher-priority READY task
// (prio 11 > harness 10) exists.
// Input: Real task (prio 11) added to the ready queue.
// Expect: Scheduler::needs_switch() returns true.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_higher_priority, "PRE: none | POST: none") {
    auto *high = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == true);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: needs_switch() returns false when the only READY task has the
// same priority as the current harness (round-robin handled by tick).
// Input: Real task (prio 10, equal to harness) added.
// Expect: needs_switch() returns false.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_equal_priority, "PRE: none | POST: none") {
    auto *equal = TaskControlBlock::create([]() {}, 10, 10);
    JARVIS_ASSERT(equal != nullptr);
    Scheduler::add_task(*equal);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == false);

    Scheduler::remove_task(*equal);
    equal->cleanup();
    delete equal;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: needs_switch() returns false when a higher-priority task exists
// but is BLOCKED (not runnable) — reached through a real blocking operation.
// Input: Real task (prio 11) genuinely blocks on a semaphore.
// Expect: needs_switch() returns false.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_needs_switch_blocked_higher, "PRE: none | POST: none") {
    sync::Semaphore gate;
    gate.init(0, 1);
    auto *high = TaskControlBlock::create(
        []() {
            sync::Semaphore *g = reinterpret_cast<sync::Semaphore *>(
                Scheduler::current_task()->user_data);
            g->wait();
        },
        11, 10);
    JARVIS_ASSERT(high != nullptr);
    high->user_data = &gate;
    Scheduler::add_task(*high);
    Scheduler::reschedule();
    while (high->state != TaskState::BLOCKED)
        asm volatile("pause");
    JARVIS_ASSERT(high->state == TaskState::BLOCKED);

    bool result = Scheduler::needs_switch();
    JARVIS_ASSERT(result == false);

    gate.post();
    while (high->state != TaskState::TERMINATED)
        asm volatile("pause");
    release_task(high);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: set_preemptible(false) causes needs_switch() to return false even
// with a higher-priority READY task present.
// Input: Real task (prio 11) added; preemption disabled.
// Expect: needs_switch() returns false while preemption is off.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_disabled_blocks_switch, "PRE: none | POST: none") {
    Scheduler::set_preemptible(false);

    auto *high = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(high != nullptr);
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
// Testidea: Verifies toggling preemption on/off repeatedly does not crash and
// state remains consistent.
// Input: Loop 100 times: set_preemptible(true), set_preemptible(false).
// Expect: No crash; is_preemptible() reflects each toggle correctly.
// Depends: kernel::task::Scheduler
JARVIS_TEST(preemption_interrupt_enable_disable_cycle,
            "PRE: none | POST: none") {
    for (uint64_t i = 0; i < 100; ++i) {
        Scheduler::set_preemptible(true);
        JARVIS_ASSERT(Scheduler::is_preemptible() == true);

        Scheduler::set_preemptible(false);
        JARVIS_ASSERT(Scheduler::is_preemptible() == false);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: A REAL periodic task (prio 11, period 5) that runs past a full
// period observes its remaining_ticks reload from 0 back to period — proving
// the real on_tick quantum-accounting reload path.
// Input: Real task busy-waits ~10 real ticks while polling remaining_ticks.
// Expect: remaining_ticks reloads (jumps up after reaching 0).
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_quantum_exhaustion, "PRE: none | POST: none") {
    static volatile bool g_reloaded = false;

    auto *t = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            uint64_t prev = self->remaining_ticks;
            for (int i = 0; i < 40 && !g_reloaded; ++i) {
                uint64_t cur = self->remaining_ticks;
                if (cur > prev)
                    g_reloaded = true; // reloaded from 0 back to period
                prev = cur;
                arch::pause();
            }
        },
        11, 5);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    while (t->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT(g_reloaded);
    release_task(t);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the real RMS dispatch picks a higher-priority READY task
// and does not dispatch the harness to itself.
// Input: Real task (prio 11) added; the harness calls reschedule() and the
//        timer ISR dispatches the higher-priority task.
// Expect: The higher-priority task genuinely runs (completes).
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(preemption_task_switch_does_not_switch_to_self,
            "PRE: none | POST: none") {
    static uint64_t g_ran = 0;
    auto *other = TaskControlBlock::create(
        []() { g_ran = 1; }, 11, 10);
    JARVIS_ASSERT(other != nullptr);
    Scheduler::add_task(*other);

    Scheduler::reschedule();
    while (other->state != TaskState::TERMINATED)
        asm volatile("pause");
    JARVIS_ASSERT_EQ(1ULL, g_ran);

    release_task(other);
    JARVIS_TEST_PASS();
}

void register_preemption_tests() {
    Logger::info("Registering preemption tests");
    JARVIS_REGISTER_TEST(preemption_needs_switch_higher_priority);
    JARVIS_REGISTER_TEST(preemption_needs_switch_equal_priority);
    JARVIS_REGISTER_TEST(preemption_needs_switch_blocked_higher);
    JARVIS_REGISTER_TEST(preemption_disabled_blocks_switch);
    JARVIS_REGISTER_TEST(preemption_interrupt_enable_disable_cycle);
    JARVIS_REGISTER_TEST(preemption_quantum_exhaustion);
    JARVIS_REGISTER_TEST(preemption_task_switch_does_not_switch_to_self);
}
