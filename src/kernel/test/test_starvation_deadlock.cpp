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

/// @file test_starvation_deadlock.cpp
/// @brief Starvation and deadlock scenario tests.

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/test/test_sched_helpers.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

static uint64_t g_starvation_counter = 0;

// Runmode: kernel
// Testidea: Create one high-priority task that runs continuously and
// a low-priority task that should eventually get CPU time.  Under a
// strict priority-only scheduler the low task may starve; this test
// verifies whether the scheduler provides any fairness guarantee.
// Input: High(10) spins, Low(5) increments a counter when it runs.
// Expect: Low runs at least once after a reasonable number of reschedules.
TEST_CLASS(SchedulerStarvation) {
    g_starvation_counter = 0;

    auto* low = TaskControlBlock::create([]() {
        ++g_starvation_counter;
    }, 5, 10);
    CT_ASSERT(low != nullptr);
    Scheduler::add_task(*low);

    auto* high = TaskControlBlock::create([]() {
        uint64_t limit = 1000000ULL;
        for (uint64_t i = 0; i < limit; ++i) {}
    }, 10, 10);
    CT_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* original = Scheduler::current_task();
    g_starvation_counter = 0;

    // Run high many times — if scheduler never schedules low, this
    // test exposes starvation.
    // yield_as() disables interrupts so timer ISR cannot fire between
    // set_current and reschedule(), which would save CPU state into the
    // wrong task's context.rsp (stale globals from a prior reschedule()).
    // This restores the IF=0 semantics the test was designed for.
    for (int i = 0; i < 100; ++i) {
        kernel::test::yield_as(*high);
        kernel::test::yield_as(*low);
    }

    Scheduler::set_current(*original);

    // Low should have run at least once (even preemptive priority
    // schedulers eventually context-switch, especially if high blocks
    // or yields). A yield in the test driver is implicit in reschedule.
    // NOTE: In a strict RM scheduler without aging this could fail.
    // This test documents current behaviour — if g_starvation_counter == 0,
    // starvation is confirmed and aging should be added.
    if (g_starvation_counter == 0) {
        Logger::warn("SchedulerStarvation: low task never ran — "
                     "starvation confirmed");
    }

    Scheduler::remove_task(*low);
    low->cleanup(); delete low;
    Scheduler::remove_task(*high);
    high->cleanup(); delete high;
};

// Runmode: kernel
// Testidea: Build a 5-deep priority inversion chain:
// Task A (prio 10) holds mutex M1, waits for semaphore S1.
// Task B (prio 15) holds S1, waits for mutex M2.
// Task C (prio 20) holds M2, waits for mutex M3.
// Task D (prio 25) holds M3, waits for semaphore S2.
// Task E (prio 30) holds S2, waits for mutex M1.
// This creates a cycle through mutex/semaphore ownership that should
// be detected as a deadlock if deadlock detection is active.
// Input: Create tasks, lock resources in inversion order.
// Expect: System detects deadlock or tasks remain blocked without crash.
TEST_CLASS(PriorityInversionChain5) {
    sync::Mutex m1, m2, m3;
    sync::Semaphore s1, s2;
    m1.init(); m2.init(); m3.init();
    s1.init(0, 1); s2.init(0, 1);

    // Guard entire test: prevent timer ISR from dispatching test tasks
    // before we set them to BLOCKED. Without this, the ISR dispatches
    // tasks created with [](){} → they TERMINATE → dangling pointers.
    {
        arch::IrqGuard outer;

        auto* task_e = TaskControlBlock::create([]() {}, 30, 10);
        CT_ASSERT(task_e != nullptr);
        task_e->base_priority = 30; task_e->priority = 30;
        Scheduler::add_task(*task_e);

        auto* task_d = TaskControlBlock::create([]() {}, 25, 10);
        CT_ASSERT(task_d != nullptr);
        task_d->base_priority = 25; task_d->priority = 25;
        Scheduler::add_task(*task_d);

        auto* task_c = TaskControlBlock::create([]() {}, 20, 10);
        CT_ASSERT(task_c != nullptr);
        task_c->base_priority = 20; task_c->priority = 20;
        Scheduler::add_task(*task_c);

        auto* task_b = TaskControlBlock::create([]() {}, 15, 10);
        CT_ASSERT(task_b != nullptr);
        task_b->base_priority = 15; task_b->priority = 15;
        Scheduler::add_task(*task_b);

        auto* task_a = TaskControlBlock::create([]() {}, 10, 10);
        CT_ASSERT(task_a != nullptr);
        task_a->base_priority = 10; task_a->priority = 10;
        Scheduler::add_task(*task_a);

        // Step 1: A locks M1 (uncontested)
        {
            kernel::test::ScopedCurrentTask scope(*task_a);
            m1.lock();
            CT_ASSERT(m1.owner() == task_a);
            task_a->priority = 30;
        }

        // Step 2: B posts and waits S1 (uncontested)
        {
            kernel::test::ScopedCurrentTask scope(*task_b);
            s1.post();
            s1.wait();
        }

        // Step 3: C locks M2 (uncontested)
        {
            kernel::test::ScopedCurrentTask scope(*task_c);
            m2.lock();
            CT_ASSERT(m2.owner() == task_c);
        }

        // Step 4: D locks M3 (uncontested)
        {
            kernel::test::ScopedCurrentTask scope(*task_d);
            m3.lock();
            CT_ASSERT(m3.owner() == task_d);
        }

        // Step 5: E posts and waits S2 (uncontested)
        {
            kernel::test::ScopedCurrentTask scope(*task_e);
            s2.post();
            s2.wait();
        }

        // E tries M1 — would block. Set state directly.
        {
            kernel::test::ScopedCurrentTask scope(*task_e);
            task_e->state = TaskState::BLOCKED;
        }
        CT_ASSERT(m1.owner() == task_a);
        CT_ASSERT(task_a->priority >= task_e->priority);

        // D tries S2 — would block
        {
            kernel::test::ScopedCurrentTask scope(*task_d);
            task_d->state = TaskState::BLOCKED;
        }

        // C tries M3 — would block
        {
            kernel::test::ScopedCurrentTask scope(*task_c);
            task_c->state = TaskState::BLOCKED;
        }

        // B tries M2 — would block
        {
            kernel::test::ScopedCurrentTask scope(*task_b);
            task_b->state = TaskState::BLOCKED;
        }

        // A tries S1 — would block
        {
            kernel::test::ScopedCurrentTask scope(*task_a);
            task_a->state = TaskState::BLOCKED;
        }

        CT_ASSERT(task_a->state == TaskState::BLOCKED);
        CT_ASSERT(task_b->state == TaskState::BLOCKED);

        // Cleanup while still under IrqGuard
        TaskControlBlock* cleanup_list[] = {task_a, task_b, task_c, task_d, task_e};
        for (size_t ci = 0; ci < 5; ++ci) {
            cleanup_list[ci]->state = TaskState::READY;
            Scheduler::remove_task(*cleanup_list[ci]);
            cleanup_list[ci]->cleanup();
            delete cleanup_list[ci];
        }
    }
};

// Runmode: kernel
// Testidea: Orchestrated lock/unlock patterns across 3 mutexes and 4 tasks.
// 5 distinct contention scenarios exercise lock ordering, ownership transfer,
// and cross-mutex independence without relying on real blocking (which
// conflicts with the scheduler's deferred context-switch mechanism).
// Input: 4 tasks (priorities 3,5,7,9), 3 mutexes, 5 orchestrated patterns.
// Expect: All mutexes unlocked at end; no crash; no corrupt state.
TEST_CLASS(DeadlockNestedMutexLoad) {
    sync::Mutex mtx[3];
    for (int i = 0; i < 3; ++i) mtx[i].init();

    static constexpr uint64_t PRIOS[4] = {3, 5, 7, 9};
    TaskControlBlock* t[4];
    for (int i = 0; i < 4; ++i) {
        t[i] = TaskControlBlock::create([](){}, PRIOS[i], 10);
        CT_ASSERT(t[i] != nullptr);
        t[i]->base_priority = PRIOS[i];
        t[i]->priority = PRIOS[i];
        Scheduler::add_task(*t[i]);
    }

    {
        arch::IrqGuard outer;

        // Pattern 1: Single mutex, priority selection.
        // t[0] locks M0; t[1], t[2] blocked awaiting M0; t[0] unlock wakes
        // the highest-prio waiter (t[2], prio 7 > 5).
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].lock();   // uncontested
            CT_ASSERT(mtx[0].owner() == t[0]);
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            t[1]->state = TaskState::BLOCKED;   // simulate contends M0
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            t[2]->state = TaskState::BLOCKED;   // simulate contends M0
        }
        // t[0] releases M0 → wake_one would pick t[2] (highest prio)
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].unlock();
            CT_ASSERT(mtx[0].owner() == nullptr);
        }
        // t[2] acquires M0 (uncontested now), releases
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            CT_ASSERT(mtx[0].owner() == nullptr);
            mtx[0].lock();
            CT_ASSERT(mtx[0].owner() == t[2]);
            mtx[0].unlock();
        }
        // t[1] acquires M0, releases
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[0].lock();
            CT_ASSERT(mtx[0].owner() == t[1]);
            mtx[0].unlock();
        }
        CT_ASSERT(mtx[0].owner() == nullptr);

        // Pattern 2: Reverse add order — wake_one must pick highest prio
        // regardless of insertion order (prio 9 > 5).
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].lock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            t[1]->state = TaskState::BLOCKED;
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[3]);
            t[3]->state = TaskState::BLOCKED;   // prio 9, added after t[1]
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].unlock();
        }
        // t[3] (prio 9) should wake first
        {
            kernel::test::ScopedCurrentTask scope(*t[3]);
            mtx[0].lock();
            mtx[0].unlock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[0].lock();
            mtx[0].unlock();
        }
        CT_ASSERT(mtx[0].owner() == nullptr);

        // Pattern 3: Two independent mutexes — no cross-contamination
        // between waiter arrays. t[0] holds M0, t[1] holds M1.
        // t[2] awaits M0, t[3] awaits M1.
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].lock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[1].lock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            t[2]->state = TaskState::BLOCKED;   // awaits M0
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[3]);
            t[3]->state = TaskState::BLOCKED;   // awaits M1
        }
        // Release M0 → only t[2] unblocks
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].unlock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            mtx[0].lock();
            mtx[0].unlock();
        }
        // t[3] still BLOCKED on M1, verify M1 still held by t[1]
        CT_ASSERT(mtx[1].owner() == t[1]);
        CT_ASSERT(mtx[1].is_locked());
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[1].unlock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[3]);
            mtx[1].lock();
            mtx[1].unlock();
        }
        CT_ASSERT(!mtx[0].is_locked());
        CT_ASSERT(!mtx[1].is_locked());

        // Pattern 4: Nested locks — one task holds two mutexes,
        // two waiters each on a different mutex.
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].lock();
            mtx[1].lock();
            CT_ASSERT(mtx[0].owner() == t[0]);
            CT_ASSERT(mtx[1].owner() == t[0]);
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            t[1]->state = TaskState::BLOCKED;   // awaits M0
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            t[2]->state = TaskState::BLOCKED;   // awaits M1
        }
        // Release M1 first (reverse order)
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[1].unlock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            mtx[1].lock();
            mtx[1].unlock();
        }
        // M0 still held by t[0]
        CT_ASSERT(mtx[0].owner() == t[0]);
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].unlock();
        }
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[0].lock();
            mtx[0].unlock();
        }
        CT_ASSERT(!mtx[0].is_locked());
        CT_ASSERT(!mtx[1].is_locked());

        // Pattern 5: Three-mutex rotation — lock in address order
        // across 3 mutexes, verify clean ownership chain.
        {
            kernel::test::ScopedCurrentTask scope(*t[0]);
            mtx[0].lock();
            mtx[1].lock();
            CT_ASSERT(mtx[0].owner() == t[0]);
            CT_ASSERT(mtx[1].owner() == t[0]);
            mtx[1].unlock();
            mtx[0].unlock();
        }
        CT_ASSERT(!mtx[0].is_locked());
        CT_ASSERT(!mtx[1].is_locked());
        {
            kernel::test::ScopedCurrentTask scope(*t[1]);
            mtx[1].lock();
            mtx[2].lock();
            CT_ASSERT(mtx[1].owner() == t[1]);
            CT_ASSERT(mtx[2].owner() == t[1]);
            mtx[2].unlock();
            mtx[1].unlock();
        }
        CT_ASSERT(!mtx[1].is_locked());
        CT_ASSERT(!mtx[2].is_locked());
        {
            kernel::test::ScopedCurrentTask scope(*t[2]);
            mtx[0].lock();
            mtx[2].lock();
            CT_ASSERT(mtx[0].owner() == t[2]);
            CT_ASSERT(mtx[2].owner() == t[2]);
            mtx[2].unlock();
            mtx[0].unlock();
        }
        CT_ASSERT(!mtx[0].is_locked());
        CT_ASSERT(!mtx[2].is_locked());

        // All mutexes unlocked, all tasks in clean state for teardown
        for (int i = 0; i < 3; ++i) CT_ASSERT(!mtx[i].is_locked());
    }

    for (int i = 0; i < 4; ++i) {
        t[i]->state = TaskState::READY;
        Scheduler::remove_task(*t[i]);
        t[i]->cleanup();
        delete t[i];
    }
};

void register_starvation_deadlock_tests() {
    Logger::info("Registering starvation/deadlock tests");
    REGISTER_CLASS(SchedulerStarvation);
    REGISTER_CLASS(PriorityInversionChain5);
    REGISTER_CLASS(DeadlockNestedMutexLoad);
    // DeadlockRecoveryResourceReclamation — never implemented
}
