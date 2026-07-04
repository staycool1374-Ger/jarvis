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
#include <scope_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

extern "C" void scheduler_on_context_switch();

// Runmode: kernel
// Testidea: After reschedule(), context-switch globals are set consistently.
// Input: Create two tasks, call reschedule(), inspect atomics.
// Expect: save_rsp_to is non-null (save target set), load_rsp_from is
//         next task's stack RSP, load_cr3_from is non-zero.
// Depends: Scheduler, context-switch atomics
JARVIS_TEST(atomic_globals_set_on_reschedule) {
    auto* task_a = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_a != nullptr);
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_b != nullptr);
    Scheduler::add_task(*task_b);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*task_a);
        task_a->cleanup();
        delete task_a;
        Scheduler::remove_task(*task_b);
        task_b->cleanup();
        delete task_b;
    });

    auto* original = Scheduler::current_task();

    Scheduler::set_current(*task_a);
    Scheduler::reschedule();

    uint64_t* saved_rsp_ptr = __atomic_load_n(
        &kernel::scheduler_save_rsp_to, __ATOMIC_ACQUIRE);
    uint64_t loaded_rsp = __atomic_load_n(
        &kernel::scheduler_load_rsp_from, __ATOMIC_ACQUIRE);
    uint64_t loaded_cr3 = __atomic_load_n(
        &kernel::scheduler_load_cr3_from, __ATOMIC_ACQUIRE);

    JARVIS_ASSERT_FMT(saved_rsp_ptr != nullptr,
                      "save_rsp_to should be non-null, got nullptr");
    JARVIS_ASSERT_FMT(loaded_rsp != 0,
                      "load_rsp_from should be non-zero, got 0");
    JARVIS_ASSERT_FMT(loaded_cr3 != 0,
                      "load_cr3_from should be non-zero, got 0");

    Scheduler::set_current(*original);

    guard.dismiss();
    Scheduler::remove_task(*task_a);
    task_a->cleanup();
    delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup();
    delete task_b;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: fpu_owner can be read/written atomically.
// Input: Set fpu_owner to a task pointer, read it back.
// Expect: Value matches what was written.
// Depends: fpu_owner atomic
JARVIS_TEST(atomic_fpu_owner_read_write) {
    auto* old = __atomic_load_n(&kernel::fpu_owner, __ATOMIC_ACQUIRE);
    auto* test_ptr = reinterpret_cast<TaskControlBlock*>(uintptr_t(0xDEADBEEF));
    __atomic_store_n(&kernel::fpu_owner, test_ptr, __ATOMIC_RELEASE);
    auto* val = __atomic_load_n(&kernel::fpu_owner, __ATOMIC_ACQUIRE);
    JARVIS_ASSERT(val == test_ptr);
    __atomic_store_n(&kernel::fpu_owner, old, __ATOMIC_RELEASE);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: scheduler_next_task_id increments atomically.
// Input: Save, store new, verify, restore.
// Expect: Atomic operations preserve the value.
// Depends: scheduler_next_task_id atomic
JARVIS_TEST(atomic_next_task_id_consistency) {
    uint64_t old = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    __atomic_store_n(&kernel::scheduler_next_task_id, old + 100, __ATOMIC_RELEASE);
    uint64_t val = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    JARVIS_ASSERT_EQ(old + 100, val);
    __atomic_store_n(&kernel::scheduler_next_task_id, old, __ATOMIC_RELEASE);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Context-switch globals survive atomic clear-and-validate cycle.
// Input: Reschedule, verify globals are set. Clear them atomically, verify.
// Expect: Globals are non-zero after reschedule, zero after atomic clear.
// Depends: Scheduler, context-switch atomics
JARVIS_TEST(atomic_idempotent_null_handling) {
    auto* task_a = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_a != nullptr);
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_b != nullptr);
    Scheduler::add_task(*task_b);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*task_a);
        task_a->cleanup();
        delete task_a;
        Scheduler::remove_task(*task_b);
        task_b->cleanup();
        delete task_b;
    });

    auto* original = Scheduler::current_task();

    Scheduler::set_current(*task_a);
    Scheduler::reschedule();

    // Globals must be set after reschedule (save target, load RSP, load CR3)
    uint64_t* save_rsp_ptr = __atomic_load_n(
        &kernel::scheduler_save_rsp_to, __ATOMIC_ACQUIRE);
    JARVIS_ASSERT_FMT(save_rsp_ptr != nullptr,
                      "save_rsp_to should be non-null after reschedule, got nullptr");

    // Clear globals atomically and verify clear took effect
    __atomic_store_n(&kernel::scheduler_save_rsp_to, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&kernel::scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&kernel::scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);

    save_rsp_ptr = __atomic_load_n(
        &kernel::scheduler_save_rsp_to, __ATOMIC_ACQUIRE);
    uint64_t load_cr3 = __atomic_load_n(
        &kernel::scheduler_load_cr3_from, __ATOMIC_ACQUIRE);

    JARVIS_ASSERT_EQ(nullptr, save_rsp_ptr);
    JARVIS_ASSERT_EQ(0ULL, load_cr3);

    Scheduler::set_current(*original);
    guard.dismiss();
    Scheduler::remove_task(*task_a);
    task_a->cleanup();
    delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup();
    delete task_b;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: fpu_owner can be read/written with relaxed memory ordering.
// Input: Relaxed load/store on fpu_owner in #NM handler context.
// Expect: Value written with relaxed store is readable with relaxed load.
// Depends: fpu_owner atomic
JARVIS_TEST(atomics_fpu_owner_relaxed) {
    TaskControlBlock* old = __atomic_load_n(&kernel::fpu_owner, __ATOMIC_RELAXED);

    auto* test_ptr = reinterpret_cast<TaskControlBlock*>(uintptr_t(0xBEEF));
    __atomic_store_n(&kernel::fpu_owner, test_ptr, __ATOMIC_RELAXED);

    TaskControlBlock* val = __atomic_load_n(&kernel::fpu_owner, __ATOMIC_RELAXED);

    JARVIS_ASSERT(val == test_ptr);

    __atomic_store_n(&kernel::fpu_owner, old, __ATOMIC_RELAXED);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: C bridge functions for isr_stubs.asm work correctly.
// Input: Set scheduler_next_task_id, call scheduler_on_context_switch().
// Expect: Current task updates to match the set ID; next_task_id cleared.
// Depends: scheduler_on_context_switch, scheduler_next_task_id
JARVIS_TEST(atomics_assembly_bridge) {
    auto* original = Scheduler::current_task();

    auto* task = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task != nullptr);
    Scheduler::add_task(*task);

    // Simulate what switch_to_task + isr_common do:
    // Set scheduler_next_task_id to the target task ID
    uint64_t old_id = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    __atomic_store_n(&kernel::scheduler_next_task_id, task->id, __ATOMIC_RELEASE);

    // Call the bridge function that isr_stubs.asm invokes
    scheduler_on_context_switch();

    // Verify current task was updated by the bridge
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT_EQ(task->id, after->id);

    // Verify scheduler_next_task_id was cleared to 0
    uint64_t cleared = __atomic_load_n(&kernel::scheduler_next_task_id, __ATOMIC_ACQUIRE);
    JARVIS_ASSERT_EQ(0ULL, cleared);

    // Restore original task and next_task_id
    Scheduler::set_current(*original);
    __atomic_store_n(&kernel::scheduler_next_task_id, old_id, __ATOMIC_RELEASE);

    Scheduler::remove_task(*task);
    task->cleanup();
    delete task;

    JARVIS_TEST_PASS();
}

void register_atomic_context_switch_tests() {
    Logger::info("Registering atomic context-switch tests");
    JARVIS_REGISTER_TEST(atomic_globals_set_on_reschedule);
    JARVIS_REGISTER_TEST(atomic_fpu_owner_read_write);
    JARVIS_REGISTER_TEST(atomic_next_task_id_consistency);
    JARVIS_REGISTER_TEST(atomic_idempotent_null_handling);
    JARVIS_REGISTER_TEST(atomics_fpu_owner_relaxed);
    JARVIS_REGISTER_TEST(atomics_assembly_bridge);
}
