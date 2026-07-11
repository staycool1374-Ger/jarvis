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

/// @file test_priority_inheritance.cpp
/// @brief Phase 6 tests: Priority Inheritance Protocol — single-level and
///        transitive priority donation across Mutex and Semaphore.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/irq_guard.hpp>

using namespace kernel;

// NOTE: The test framework runs with interrupts enabled (timer ISR at
// 1000 Hz).  Scheduler::reschedule() after a contended lock() sets
// deferred-switch globals that the ISR exit consumes on the next tick,
// causing an unintended real context switch.  To prevent this, each test
// wraps its operations in arch::IrqGuard (cli/sti).  Blocking calls modify
// task state and call reschedule(), but the guard prevents the ISR from
// consuming the deferred globals until the test finishes and the final
// set_current() clears them.

// ---------------------------------------------------------------------------
// Mutex — basic priority donation
// ---------------------------------------------------------------------------
// Low (prio 5) holds mutex, High (prio 15) tries to lock.
// Low's priority boosted to High's level; on unlock, restored.
TEST_CLASS(MutexPriorityDonates) {
    arch::IrqGuard irq_guard;
    sync::Mutex mutex;
    mutex.init();

    auto* low = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    auto* original = Scheduler::current_task();

    Scheduler::set_current(*low);
    mutex.lock();
    CT_ASSERT(mutex.owner() == low);
    CT_ASSERT(low->priority == 5);

    auto* high = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    mutex.lock();

    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(high->waiting_on_mutex == &mutex);
    CT_ASSERT(low->priority >= high->priority);

    Scheduler::set_current(*low);
    mutex.unlock();
    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high->state == TaskState::READY);
    CT_ASSERT(high->waiting_on_mutex == nullptr);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
};

// ---------------------------------------------------------------------------
// Mutex — transitive chain A → B → C
// ---------------------------------------------------------------------------
// A(5) holds M1, B(10) holds M2 then blocks on M1, C(15) blocks on M2.
// B boosted to C's priority, A boosted to B's boosted priority.
TEST_CLASS(MutexChainPropagates) {
    arch::IrqGuard irq_guard;
    sync::Mutex m1, m2;
    m1.init();
    m2.init();

    auto* original = Scheduler::current_task();

    auto* a = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(a != nullptr);
    a->base_priority = 5;
    a->priority = 5;
    Scheduler::add_task(*a);

    Scheduler::set_current(*a);
    m1.lock();

    auto* b = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(b != nullptr);
    b->base_priority = 10;
    b->priority = 10;
    Scheduler::add_task(*b);

    Scheduler::set_current(*b);
    m2.lock();
    CT_ASSERT(m2.owner() == b);
    m1.lock();
    CT_ASSERT(b->state == TaskState::BLOCKED);
    CT_ASSERT(b->waiting_on_mutex == &m1);
    // A boosted to B's priority
    CT_ASSERT(a->priority >= b->priority);

    auto* c = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(c != nullptr);
    c->base_priority = 15;
    c->priority = 15;
    Scheduler::add_task(*c);

    Scheduler::set_current(*c);
    m2.lock();
    CT_ASSERT(c->state == TaskState::BLOCKED);
    // B boosted to C's priority
    CT_ASSERT(b->priority >= c->priority);
    // A transitively boosted to B's boosted priority
    CT_ASSERT(a->priority >= b->priority);

    Scheduler::set_current(*a);
    m1.unlock();
    CT_ASSERT(b->state == TaskState::READY);
    CT_ASSERT(b->waiting_on_mutex == nullptr);

    Scheduler::set_current(*b);
    m2.unlock();
    CT_ASSERT(c->state == TaskState::READY);

    CT_ASSERT(a->priority == a->base_priority);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*c);
    c->cleanup();
    delete c;
    Scheduler::remove_task(*b);
    b->cleanup();
    delete b;
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
};

// ---------------------------------------------------------------------------
// Mutex — priority steps down with remaining waiters
// ---------------------------------------------------------------------------
// Holder (5), waiters at 10, 15, 20.  After waking 20, holder stays at
// max remaining waiter priority (15).
TEST_CLASS(MutexPriStepDown) {
    arch::IrqGuard irq_guard;
    sync::Mutex mutex;
    mutex.init();

    auto* original = Scheduler::current_task();

    auto* holder = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(holder != nullptr);
    holder->base_priority = 5;
    holder->priority = 5;
    Scheduler::add_task(*holder);

    Scheduler::set_current(*holder);
    mutex.lock();

    auto* w10 = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(w10 != nullptr);
    Scheduler::add_task(*w10);
    auto* w15 = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(w15 != nullptr);
    Scheduler::add_task(*w15);
    auto* w20 = TaskControlBlock::create([]() {}, 20, 10);
    CT_ASSERT(w20 != nullptr);
    Scheduler::add_task(*w20);

    Scheduler::set_current(*w10);
    mutex.lock();
    Scheduler::set_current(*w15);
    mutex.lock();
    Scheduler::set_current(*w20);
    mutex.lock();
    CT_ASSERT(holder->priority >= 20);

    // Unlock wakes w20, holder should drop to 15
    Scheduler::set_current(*holder);
    mutex.unlock();
    CT_ASSERT(holder->priority >= 15);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*w20);
    w20->cleanup();
    delete w20;
    Scheduler::remove_task(*w15);
    w15->cleanup();
    delete w15;
    Scheduler::remove_task(*w10);
    w10->cleanup();
    delete w10;
    Scheduler::remove_task(*holder);
    holder->cleanup();
    delete holder;
};

// ---------------------------------------------------------------------------
// Mutex — nested mutexes held by same task
// ---------------------------------------------------------------------------
// A holds M1 (waiter 10) and M2 (waiter 20).  After M2 unlock, A drops
// to 10.  After M1 unlock, A returns to base 5.
TEST_CLASS(MutexNestedDrop) {
    arch::IrqGuard irq_guard;
    sync::Mutex m1, m2;
    m1.init();
    m2.init();

    auto* original = Scheduler::current_task();

    auto* a = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(a != nullptr);
    a->base_priority = 5;
    a->priority = 5;
    Scheduler::add_task(*a);

    Scheduler::set_current(*a);
    m1.lock();
    m2.lock();

    auto* w10 = TaskControlBlock::create([]() {}, 10, 10);
    CT_ASSERT(w10 != nullptr);
    Scheduler::add_task(*w10);
    auto* w20 = TaskControlBlock::create([]() {}, 20, 10);
    CT_ASSERT(w20 != nullptr);
    Scheduler::add_task(*w20);

    Scheduler::set_current(*w10);
    m1.lock();
    Scheduler::set_current(*w20);
    m2.lock();
    CT_ASSERT(a->priority >= 20);

    // Release M2 → still at 10 (M1 waiter remains)
    m2.unlock();
    CT_ASSERT(a->priority >= 10);

    // Release M1 → back to base
    Scheduler::set_current(*a);
    m1.unlock();
    CT_ASSERT(a->priority == a->base_priority);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*w20);
    w20->cleanup();
    delete w20;
    Scheduler::remove_task(*w10);
    w10->cleanup();
    delete w10;
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
};

// ---------------------------------------------------------------------------
// Semaphore — priority inheritance
// ---------------------------------------------------------------------------
// Low (prio 5) holds binary semaphore (count→0), High (prio 15) waits.
// Low boosted; on post priority restored.
TEST_CLASS(SemaphoreInherits) {
    arch::IrqGuard irq_guard;
    sync::Semaphore sem;
    sem.init(1, 1);

    auto* original = Scheduler::current_task();

    auto* low = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    Scheduler::set_current(*low);
    sem.wait();
    CT_ASSERT(low->priority == 5);

    auto* high = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    sem.wait();
    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(low->priority >= high->priority);

    Scheduler::set_current(*original);
    sem.post();
    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high->state == TaskState::READY);

    Scheduler::set_current(*low);
    sem.post();

    Scheduler::set_current(*original);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
};

void register_priority_inheritance_tests() {
    Logger::info("Registering priority inheritance tests");
    REGISTER_CLASS(MutexPriorityDonates);
    REGISTER_CLASS(MutexChainPropagates);
    REGISTER_CLASS(MutexPriStepDown);
    REGISTER_CLASS(MutexNestedDrop);
    REGISTER_CLASS(SemaphoreInherits);
}
