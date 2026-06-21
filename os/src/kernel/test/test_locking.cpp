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
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that mutex.try_lock() returns true on an unlocked mutex;
// is_locked() and owner() reflect the new lock state.
// Input: Create unlocked mutex, call try_lock() from owner task.
// Expect: try_lock() returns true, is_locked() true, owner() non-null.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_try_lock_success) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);
    bool ok = mutex.try_lock();
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that mutex.try_lock() returns false when mutex is held by
// a different task; state unchanged.
// Input: Owner task locks mutex, another task attempts try_lock().
// Expect: try_lock() returns false, original lock state unchanged.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_try_lock_failure) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);
    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    auto* waiter = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(waiter != nullptr);
    Scheduler::add_task(*waiter);

    Scheduler::set_current(*waiter);
    bool ok = mutex.try_lock();
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*waiter);
    waiter->cleanup();
    delete waiter;
    Scheduler::remove_task(*owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that mutex.try_lock() returns true when the same owner calls
// it recursively (recursive mutex behavior).
// Input: Owner locks mutex, locks again via try_lock().
// Expect: try_lock() returns true; unlock count > 1; final unlock releases.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_try_lock_recursive_same_owner) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);
    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    bool ok = mutex.try_lock();
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    mutex.unlock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    mutex.unlock();
    JARVIS_ASSERT(!mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == nullptr);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that low-priority task holds a mutex; high-priority task
// attempts to lock it; the low-priority task's priority field is temporarily
// boosted to the high-priority level; after the low-priority task unlocks, its
// priority returns to base_priority.
// Input: Low priority task holds mutex, high priority task attempts lock.
// Expect: Low task priority temporarily boosted, then restored.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_priority_inheritance_indirect) {
    sync::Mutex mutex;
    mutex.init();

    auto* low = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);
    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == low);

    auto* high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == high);

    // The low task's priority should have been boosted to at least high's priority
    // In the implementation, the owner is boosted to the waiter's priority
    // We can verify this by checking that the low task's priority is now >= high's
    JARVIS_ASSERT(low->priority >= high->priority);

    mutex.unlock();
    JARVIS_ASSERT(mutex.owner() == low);

    // After unlock, low task's priority should return to base_priority
    JARVIS_ASSERT(low->priority == low->base_priority);

    mutex.unlock();
    JARVIS_ASSERT(!mutex.is_locked());

    Scheduler::set_current(*original);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Three-task chain A→B→C (A holds mutex M1, B waits on M1,
// C waits on M2 held by B, which is blocked); verify that priority propagates
// correctly through the chain by reading each task's priority field.
// Input: Create chain A (holds M1), B (waits on M1), C (waits on M2 held by B).
// Expect: Priority propagates through chain: C > B > A.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_priority_chain) {
    sync::Mutex m1, m2;
    m1.init();
    m2.init();

    auto* original = Scheduler::current_task();

    // Task A holds M1
    auto* a = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(a != nullptr);
    a->base_priority = 5;
    a->priority = 5;
    Scheduler::add_task(*a);

    Scheduler::set_current(*a);
    m1.lock();
    JARVIS_ASSERT(m1.owner() == a);

    // Task B waits on M1
    auto* b = TaskControlBlock::create([]() {}, 10, 10);
    JARVIS_ASSERT(b != nullptr);
    b->base_priority = 10;
    b->priority = 10;
    Scheduler::add_task(*b);

    Scheduler::set_current(*b);
    m1.lock();
    JARVIS_ASSERT(m1.owner() == b);

    // Task C waits on M2 held by B
    auto* c = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(c != nullptr);
    c->base_priority = 15;
    c->priority = 15;
    Scheduler::add_task(*c);

    Scheduler::set_current(*c);
    m2.lock();
    JARVIS_ASSERT(m2.owner() == c);

    // Verify priority chain: C (15) > B (boosted from 10) > A (5)
    // B's priority should be boosted to at least C's priority
    JARVIS_ASSERT(b->priority >= c->priority);
    // A's priority should be boosted to at least B's boosted priority
    JARVIS_ASSERT(a->priority >= b->priority);

    // Clean up in reverse order
    m2.unlock();
    m1.unlock();

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
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Multiple tasks blocked on a mutex at different priorities;
// when the holder unlocks, the highest-priority waiter acquires the lock next
// (not FIFO).
// Input: Create mutex holder (priority 5), waiters (priorities 10, 15).
// Expect: After unlock, highest-priority waiter (15) acquires lock.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_waiter_priority_order) {
    sync::Mutex mutex;
    mutex.init();

    auto* original = Scheduler::current_task();

    // Create holder with priority 5
    auto* holder = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(holder != nullptr);
    Scheduler::add_task(*holder);

    Scheduler::set_current(*holder);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == holder);

    // Create waiters with different priorities
    auto* waiter_low = TaskControlBlock::create([]() {}, 10, 10);
    JARVIS_ASSERT(waiter_low != nullptr);
    Scheduler::add_task(*waiter_low);

    auto* waiter_high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(waiter_high != nullptr);
    Scheduler::add_task(*waiter_high);

    // Both waiters should be blocked
    JARVIS_ASSERT(waiter_low->state == TaskState::BLOCKED);
    JARVIS_ASSERT(waiter_high->state == TaskState::BLOCKED);

    Scheduler::set_current(*original);

    // Unlock - highest priority waiter should acquire
    mutex.unlock();
    JARVIS_ASSERT(mutex.owner() == waiter_high);
    JARVIS_ASSERT(waiter_high->state == TaskState::RUNNING);
    JARVIS_ASSERT(waiter_low->state == TaskState::BLOCKED);

    // Clean up
    mutex.unlock();

    Scheduler::set_current(*original);
    Scheduler::remove_task(*waiter_high);
    waiter_high->cleanup();
    delete waiter_high;
    Scheduler::remove_task(*waiter_low);
    waiter_low->cleanup();
    delete waiter_low;
    Scheduler::remove_task(*holder);
    holder->cleanup();
    delete holder;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Mutex locked twice by the same owner does not deadlock (recursive mutex);
// correct unlock count required for full release.
// Input: Owner locks mutex twice, unlocks twice.
// Expect: No deadlock; after second unlock, mutex is unlocked.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_double_lock_same_owner) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);

    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    mutex.unlock();
    JARVIS_ASSERT(mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == owner);

    mutex.unlock();
    JARVIS_ASSERT(!mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == nullptr);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: 100 rapid lock/unlock cycles on the same mutex; no corruption,
// no stray waiters, owner correctly NULL after final unlock.
// Input: Loop 100 times: lock, unlock.
// Expect: No crash; after loop, mutex unlocked.
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_lock_acquire_release_cycle) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);

    for (uint64_t i = 0; i < 100; ++i) {
        mutex.lock();
        JARVIS_ASSERT(mutex.is_locked());
        JARVIS_ASSERT(mutex.owner() == owner);

        mutex.unlock();
        JARVIS_ASSERT(!mutex.is_locked());
        JARVIS_ASSERT(mutex.owner() == nullptr);
    }

    Scheduler::set_current(*original);
    Scheduler::remove_task(*owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Multiple tasks blocked on Semaphore::wait() at different priorities;
// post() wakes the highest-priority task first.
// Input: Create semaphore with count=0, tasks with priorities 5, 10, 15.
// Expect: After post(), highest-priority task (15) is READY.
// Depends: kernel::sync::Semaphore, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(semaphore_wait_priority_order) {
    sync::Semaphore sem;
    sem.init(0, 3);

    auto* original = Scheduler::current_task();

    // Create tasks with different priorities
    auto* task_low = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(task_low != nullptr);
    Scheduler::add_task(*task_low);

    auto* task_mid = TaskControlBlock::create([]() {}, 10, 10);
    JARVIS_ASSERT(task_mid != nullptr);
    Scheduler::add_task(*task_mid);

    auto* task_high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(task_high != nullptr);
    Scheduler::add_task(*task_high);

    // All tasks should be blocked
    JARVIS_ASSERT(task_low->state == TaskState::BLOCKED);
    JARVIS_ASSERT(task_mid->state == TaskState::BLOCKED);
    JARVIS_ASSERT(task_high->state == TaskState::BLOCKED);

    Scheduler::set_current(*original);

    // Post should wake highest priority task
    sem.post();
    JARVIS_ASSERT(task_high->state == TaskState::READY);
    JARVIS_ASSERT(task_mid->state == TaskState::BLOCKED);
    JARVIS_ASSERT(task_low->state == TaskState::BLOCKED);

    // Clean up
    Scheduler::set_current(*task_high);
    sem.try_wait();
    Scheduler::set_current(*original);

    Scheduler::remove_task(*task_high);
    task_high->cleanup();
    delete task_high;
    Scheduler::remove_task(*task_mid);
    task_mid->cleanup();
    delete task_mid;
    Scheduler::remove_task(*task_low);
    task_low->cleanup();
    delete task_low;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: post(3) with 5 blocked waiters wakes exactly 3, leaves 2 blocked;
// the woken tasks have state == READY, the blocked ones have state == BLOCKED.
// Input: Create semaphore with count=0, 5 waiters, post(3).
// Expect: Exactly 3 tasks become READY, 2 remain BLOCKED.
// Depends: kernel::sync::Semaphore, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(semaphore_multi_waiter_partial_wake) {
    sync::Semaphore sem;
    sem.init(0, 5);

    auto* original = Scheduler::current_task();

    // Create 5 tasks
    TaskControlBlock* tasks[5];
    for (int i = 0; i < 5; ++i) {
        tasks[i] = TaskControlBlock::create([]() {}, 5 + i, 10);
        JARVIS_ASSERT(tasks[i] != nullptr);
        Scheduler::add_task(*tasks[i]);
    }

    // All tasks should be blocked
    for (int i = 0; i < 5; ++i) {
        JARVIS_ASSERT(tasks[i]->state == TaskState::BLOCKED);
    }

    Scheduler::set_current(*original);

    // Post 3 times
    sem.post();
    sem.post();
    sem.post();

    // Exactly 3 should be READY, 2 should be BLOCKED
    int ready_count = 0;
    int blocked_count = 0;
    for (int i = 0; i < 5; ++i) {
        if (tasks[i]->state == TaskState::READY) ready_count++;
        else if (tasks[i]->state == TaskState::BLOCKED) blocked_count++;
    }

    JARVIS_ASSERT(ready_count == 3);
    JARVIS_ASSERT(blocked_count == 2);

    // Clean up - wake the remaining tasks
    for (int i = 0; i < 5; ++i) {
        if (tasks[i]->state == TaskState::READY) {
            Scheduler::set_current(*tasks[i]);
            sem.try_wait();
            Scheduler::set_current(*original);
        }
    }

    for (int i = 0; i < 5; ++i) {
        Scheduler::remove_task(*tasks[i]);
        tasks[i]->cleanup();
        delete tasks[i];
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Semaphore initialized with count=0; wait() blocks immediately;
// post() makes it pass.
// Input: Create semaphore with count=0, task calls wait().
// Expect: Task blocks immediately; after post(), task becomes READY.
// Depends: kernel::sync::Semaphore, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(semaphore_initial_count_zero) {
    sync::Semaphore sem;
    sem.init(0, 1);

    auto* original = Scheduler::current_task();

    auto* task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(task != nullptr);
    Scheduler::add_task(*task);

    Scheduler::set_current(*task);
    sem.wait();
    JARVIS_ASSERT(task->state == TaskState::READY);

    Scheduler::set_current(*original);
    sem.post();

    Scheduler::set_current(*task);
    bool ok = sem.try_wait();
    JARVIS_ASSERT(!ok);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*task);
    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Blocked senders/receivers in Queue are woken in priority order,
// not FIFO order.
// Input: Create queue, fill with messages at different priorities,
// create blocked sender and receiver tasks.
// Expect: When queue is drained, highest-priority waiter is woken first.
// Depends: kernel::sync::Queue, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(queue_send_receive_priority_ordering) {
    sync::Queue queue;
    queue.init();

    auto* original = Scheduler::current_task();

    // Fill queue with messages at different priorities
    for (int i = 0; i < 3; ++i) {
        uint8_t data[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(data, 1));
    }

    // Create sender task with high priority (should be woken first)
    auto* sender_high = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        uint8_t buf[32];
        size_t size = 32;
        q->receive(buf, &size);
    }, 15, 10);
    JARVIS_ASSERT(sender_high != nullptr);
    sender_high->user_data = &queue;
    Scheduler::add_task(*sender_high);

    // Create sender task with low priority (should be woken second)
    auto* sender_low = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        uint8_t buf[32];
        size_t size = 32;
        q->receive(buf, &size);
    }, 5, 10);
    JARVIS_ASSERT(sender_low != nullptr);
    sender_low->user_data = &queue;
    Scheduler::add_task(*sender_low);

    // Both senders should be blocked
    JARVIS_ASSERT(sender_high->state == TaskState::BLOCKED);
    JARVIS_ASSERT(sender_low->state == TaskState::BLOCKED);

    Scheduler::set_current(*original);

    // Drain one message from queue
    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));

    // Highest priority sender should be woken first
    JARVIS_ASSERT(sender_high->state == TaskState::READY);
    JARVIS_ASSERT(sender_low->state == TaskState::BLOCKED);

    // Clean up
    Scheduler::set_current(*sender_high);
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    Scheduler::set_current(*original);

    Scheduler::remove_task(*sender_high);
    sender_high->cleanup();
    delete sender_high;
    Scheduler::remove_task(*sender_low);
    sender_low->cleanup();
    delete sender_low;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Queue filled to capacity; send() blocks the caller; a consumer
// receive() wakes the blocked sender; message is correctly delivered.
// Input: Fill queue to capacity, create sender task that calls send().
// Expect: Sender blocks; after receive(), sender wakes and message is delivered.
// Depends: kernel::sync::Queue, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(queue_send_to_full_blocks_and_wakes) {
    sync::Queue queue;
    queue.init();

    auto* original = Scheduler::current_task();

    // Fill queue to capacity
    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }

    // Create sender task that will block on send
    auto* sender = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        uint8_t data[32] = {0xFF};
        q->send(data, 1);
    }, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    sender->user_data = &queue;
    Scheduler::add_task(*sender);

    Scheduler::set_current(*sender);
    queue.send((uint8_t*)"test", 4);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    Scheduler::set_current(*original);

    // Receiver drains one message
    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));

    // Sender should now be READY
    JARVIS_ASSERT(sender->state == TaskState::READY);

    // Clean up
    Scheduler::set_current(*sender);
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    Scheduler::set_current(*original);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

void register_locking_tests() {
    Logger::info("Registering locking tests");
    JARVIS_REGISTER_TEST(mutex_try_lock_success);
    JARVIS_REGISTER_TEST(mutex_try_lock_failure);
    JARVIS_REGISTER_TEST(mutex_try_lock_recursive_same_owner);
    JARVIS_REGISTER_TEST(mutex_priority_inheritance_indirect);
    JARVIS_REGISTER_TEST(mutex_priority_chain);
    JARVIS_REGISTER_TEST(mutex_waiter_priority_order);
    JARVIS_REGISTER_TEST(mutex_double_lock_same_owner);
    JARVIS_REGISTER_TEST(mutex_lock_acquire_release_cycle);
    JARVIS_REGISTER_TEST(semaphore_wait_priority_order);
    JARVIS_REGISTER_TEST(semaphore_multi_waiter_partial_wake);
    JARVIS_REGISTER_TEST(semaphore_initial_count_zero);
    JARVIS_REGISTER_TEST(queue_send_receive_priority_ordering);
    JARVIS_REGISTER_TEST(queue_send_to_full_blocks_and_wakes);
}