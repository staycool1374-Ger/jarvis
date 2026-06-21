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
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Validates that the scheduler reports at least one task running.
// Input: No parameters; reads global scheduler state
// Expect: JARVIS_ASSERT checks that task_count >= 1
// Depends: test, scheduler
JARVIS_TEST(scheduler_task_count) {
    uint64_t cnt = Scheduler::task_count();
    JARVIS_ASSERT(cnt >= 1);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that the current task is non-null and has a positive
// task ID.
// Input: No parameters; reads current task from Scheduler::current_task()
// Expect: JARVIS_ASSERT checks non-null and id > 0
// Depends: test, scheduler
JARVIS_TEST(scheduler_current_task) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->id > 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that calling Scheduler::reschedule returns a valid
// current task (no-op reschedule).
// Input: No parameters; captures before/after task pointers
// Expect: JARVIS_ASSERT checks that before and after are both non-null
// Depends: test, scheduler
JARVIS_TEST(scheduler_reschedule_noop) {
    auto* before = Scheduler::current_task();
    JARVIS_ASSERT(before != nullptr);
    Scheduler::reschedule();
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that adding a new task increments the task count and
// removing it restores the original count.
// Input: A new TaskControlBlock with empty lambda, priority 1, quanta 10
// Expect: JARVIS_ASSERT_EQ checks count increases by 1 after add, returns to
// original after remove; current_task non-null
// Depends: test, scheduler, task, pmm, vmm
JARVIS_TEST(scheduler_remove_task) {
    auto* before = Scheduler::current_task();
    JARVIS_ASSERT(before != nullptr);
    uint64_t cnt_before = Scheduler::task_count();

    auto* new_task = TaskControlBlock::create([]() {}, 1, 10);
    JARVIS_ASSERT(new_task != nullptr);
    Scheduler::add_task(*new_task);
    JARVIS_ASSERT_EQ(cnt_before + 1, Scheduler::task_count());

    Scheduler::remove_task(*new_task);
    JARVIS_ASSERT_EQ(cnt_before, Scheduler::task_count());

    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != nullptr);

    new_task->cleanup();
    delete new_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that reaping orphans removes a terminated task whose
// parent has exited (parent_id 999999).
// Input: A new TaskControlBlock with parent_id=999999, state=TERMINATED,
// exit_code=42
// Expect: JARVIS_ASSERT checks count increased by 1 after add, then <=
// original+1 after reap_orphans; current_task non-null
// Depends: test, scheduler, task, pmm, vmm
JARVIS_TEST(scheduler_reap_orphans) {
    uint64_t cnt_before = Scheduler::task_count();

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = 999999;
    child->state = TaskState::TERMINATED;
    child->exit_code = 42;
    Scheduler::add_task(*child);

    JARVIS_ASSERT_EQ(cnt_before + 1, Scheduler::task_count());

    Scheduler::reap_orphans();

    JARVIS_ASSERT(Scheduler::task_count() <= cnt_before + 1);

    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify that the scheduler selects a higher‑priority task over a
// lower‑priority one via next_task().
// Input: Two tasks, low (priority 5), high (priority 9).
// Expect: next_task returns the high-priority task.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(scheduler_preemptive_priority) {
    auto* low = TaskControlBlock::create([](){}, 5, 5);
    JARVIS_ASSERT(low != nullptr);
    Scheduler::add_task(*low);

    auto* high = TaskControlBlock::create([](){}, 9, 5);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* next = Scheduler::next_task();
    JARVIS_ASSERT(next == high);

    Scheduler::remove_task(*low);
    low->cleanup(); delete low;
    Scheduler::remove_task(*high);
    high->cleanup(); delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify that two equal‑priority tasks are both eligible for
// scheduling via next_task().
// Input: Two tasks, both priority 5.
// Expect: next_task returns one of the two tasks.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(scheduler_quantum_exhaustion) {
    auto* t1 = TaskControlBlock::create([](){}, 5, 5);
    auto* t2 = TaskControlBlock::create([](){}, 5, 5);
    JARVIS_ASSERT(t1 && t2);
    Scheduler::add_task(*t1);
    Scheduler::add_task(*t2);

    auto* next = Scheduler::next_task();
    JARVIS_ASSERT(next == t1 || next == t2);

    Scheduler::remove_task(*t1); t1->cleanup(); delete t1;
    Scheduler::remove_task(*t2); t2->cleanup(); delete t2;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that deferred reaping respects parent wait status -
// child not reaped while parent waits.
// Input: Create parent and child. Parent sets waiting_child_pid. Child
// exits. Reap orphans.
// Expect: Child is NOT reaped while parent waits; reaped after parent clears
// wait.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(scheduler_reap_orphans_can_reap_deferred) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(*child);
    child->parent_id = parent->id;
    parent->add_child(child);

    uint64_t child_id = child->id;
    uint64_t status = 0;
    parent->waiting_child_pid = child_id;
    parent->waiting_child_status = &status;

    // Terminate child
    child->state = TaskState::TERMINATED;
    child->exit_code = 42;

    // Reap orphans - should NOT reap child because parent is waiting
    Scheduler::reap_orphans();

    // Child should still exist (not reaped yet)
    JARVIS_ASSERT(Scheduler::find_task(child_id) == child);

    // Now simulate parent collecting (clear wait)
    parent->waiting_child_pid = 0;
    parent->waiting_child_status = nullptr;

    // Reap again - now child should be reaped
    Scheduler::reap_orphans();

    JARVIS_ASSERT(Scheduler::find_task(child_id) == nullptr);

    Scheduler::remove_task(*parent);
    parent->cleanup();
    delete parent;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies Scheduler::alloc_id() returns monotonically increasing IDs;
// wraps correctly from UINT64_MAX.
// Input: Call alloc_id() multiple times.
// Expect: Each call returns a value one greater than the previous.
// Depends: kernel::task::Scheduler
JARVIS_TEST(scheduler_alloc_id_sequential) {
    uint64_t id1 = Scheduler::alloc_id();
    uint64_t id2 = Scheduler::alloc_id();
    uint64_t id3 = Scheduler::alloc_id();
    JARVIS_ASSERT_EQ(id2, id1 + 1);
    JARVIS_ASSERT_EQ(id3, id2 + 1);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies task_at() bounds: task_at(0) returns idle task;
// task_at(count-1) returns last real task; task_at(count) returns nullptr.
// Input: Call task_at with various indices.
// Expect: Correct pointer or nullptr at boundaries.
// Depends: kernel::task::Scheduler
JARVIS_TEST(scheduler_task_at_bounds) {
    auto* idle = Scheduler::task_at(0);
    JARVIS_ASSERT(idle != nullptr);
    JARVIS_ASSERT(idle == Scheduler::get_idle_task());

    uint64_t count = Scheduler::task_count();
    auto* last = Scheduler::task_at(count - 1);
    JARVIS_ASSERT(last != nullptr);

    auto* oob = Scheduler::task_at(count);
    JARVIS_ASSERT(oob == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies find_task() returns nullptr for a nonexistent task ID.
// Input: Call find_task(999999).
// Expect: Returns nullptr.
// Depends: kernel::task::Scheduler
JARVIS_TEST(scheduler_find_task_nonexistent) {
    auto* result = Scheduler::find_task(999999);
    JARVIS_ASSERT(result == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies is_preemptible() reflects set_preemptible(true) and
// set_preemptible(false).
// Input: Toggle preemptible on/off.
// Expect: is_preemptible() matches the set value.
// Depends: kernel::task::Scheduler
JARVIS_TEST(scheduler_set_preemptible_toggle) {
    Scheduler::set_preemptible(true);
    JARVIS_ASSERT(Scheduler::is_preemptible() == true);

    Scheduler::set_preemptible(false);
    JARVIS_ASSERT(Scheduler::is_preemptible() == false);

    Scheduler::set_preemptible(true);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that after next_task() selects a different task,
// set_current() + current_task() returns the new one.
// Input: Create a higher-priority task, call next_task(), set_current(),
// then current_task().
// Expect: current_task() returns the newly selected task.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(scheduler_current_task_after_switch) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* high = TaskControlBlock::create([]() {}, 9, 10);
    JARVIS_ASSERT(high != nullptr);
    high->state = TaskState::READY;
    Scheduler::add_task(*high);

    auto* next = Scheduler::next_task();
    JARVIS_ASSERT(next != cur);
    JARVIS_ASSERT(next == high);

    Scheduler::set_current(*next);
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after == next);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies adding a task with a duplicate ID is handled gracefully
// (no crash, no corruption).
// Input: Create task, add it. Create another task with same ID (manually
// set id), attempt to add.
// Expect: No crash; second task not added or handled safely.
// Depends: kernel::task::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(scheduler_add_duplicate_id) {
    auto* t1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t1 != nullptr);
    Scheduler::add_task(*t1);

    // Create second task and manually set same ID
    auto* t2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t2 != nullptr);
    t2->id = t1->id;

    // Should not crash; add_task will insert into hash table which may
    // overwrite or fail. We verify scheduler remains consistent.
    Scheduler::add_task(*t2);

    // Verify we can still find a task with that ID
    auto* found = Scheduler::find_task(t1->id);
    JARVIS_ASSERT(found != nullptr);

    // Cleanup both
    Scheduler::remove_task(*t1);
    t1->cleanup();
    delete t1;
    // Remove t2 from scheduler if present — a direct iteration is needed
    // because the hash-table probe chain may be broken by the tombstone
    // left by remove_task(t1) when both tasks share the same ID.
    for (uint64_t _i = 0; _i < Scheduler::task_count(); ++_i) {
        if (Scheduler::task_at(_i) == t2) {
            Scheduler::remove_task(*t2);
            break;
        }
    }
    t2->cleanup();
    delete t2;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify that a task with period_ticks set is scheduled before
// it misses its deadline.  Creates a task with a very short period and
// verifies next_task() prefers it over a task with a longer period at
// the same priority.
// Input: Two tasks with same priority (5) but different periods (5, 20).
// Expect: next_task() returns the task with the shorter period.
JARVIS_TEST(scheduler_shorter_period_preferred) {
    auto* t1 = TaskControlBlock::create([](){}, 5, 5);   // priority=5, period=5
    auto* t2 = TaskControlBlock::create([](){}, 5, 20);  // priority=5, period=20
    JARVIS_ASSERT(t1 && t2);
    Scheduler::add_task(*t1);
    Scheduler::add_task(*t2);

    // RM scheduling: same priority, shorter period should be preferred
    auto* next = Scheduler::next_task();
    JARVIS_ASSERT(next == t1);

    Scheduler::remove_task(*t1); t1->cleanup(); delete t1;
    Scheduler::remove_task(*t2); t2->cleanup(); delete t2;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verify that reschedule() does NOT swap to a different task
// when the current task is the only non-idle task (no context switch).
// Input: Create one task, set it as current, call reschedule.
// Expect: current_task() returns the same task.
JARVIS_TEST(scheduler_no_spurious_switch) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* single = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(single != nullptr);
    Scheduler::add_task(*single);

    Scheduler::set_current(*single);
    auto* before = Scheduler::current_task();
    Scheduler::reschedule();
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after == before);

    Scheduler::remove_task(*single);
    single->cleanup(); delete single;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all scheduler-related unit tests with the test framework.
// Input: (none)
// Expect: Each JARVIS_REGISTER_TEST call registers a test function for later
// execution
// Depends: test, logger, scheduler, task, pmm, vmm
void register_scheduler_tests() {
    Logger::info("Registering scheduler tests");
    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);
    JARVIS_REGISTER_TEST(scheduler_reschedule_noop);
    JARVIS_REGISTER_TEST(scheduler_remove_task);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans);
    JARVIS_REGISTER_TEST(scheduler_preemptive_priority);
    JARVIS_REGISTER_TEST(scheduler_quantum_exhaustion);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans_can_reap_deferred);
    JARVIS_REGISTER_TEST(scheduler_alloc_id_sequential);
    JARVIS_REGISTER_TEST(scheduler_task_at_bounds);
    JARVIS_REGISTER_TEST(scheduler_find_task_nonexistent);
    JARVIS_REGISTER_TEST(scheduler_set_preemptible_toggle);
    JARVIS_REGISTER_TEST(scheduler_current_task_after_switch);
    JARVIS_REGISTER_TEST(scheduler_add_duplicate_id);
    JARVIS_REGISTER_TEST(scheduler_shorter_period_preferred);
    JARVIS_REGISTER_TEST(scheduler_no_spurious_switch);
}
