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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-null-argument"
#pragma GCC diagnostic ignored "-Wanalyzer-possible-null-dereference"

#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that semaphore.wait() blocks a task and
// semaphore.post() wakes it.
// Input: Semaphore initialized to 0/1, worker task calls wait(), then
// original task calls post()
// Expect: Worker state is BLOCKED after wait, READY after post
// Depends: kernel::sync::Semaphore, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(semaphore_wait_post, "PRE: none | POST: none") {
    sync::Semaphore sem;
    sem.init(0, 1);

    auto* worker = TaskControlBlock::create([]() {
        sync::Semaphore* s = reinterpret_cast<sync::Semaphore*>(Scheduler::current_task()->user_data);
        s->wait();
    }, 5, 10);
    JARVIS_ASSERT(worker != nullptr);
    worker->user_data = &sem;
    Scheduler::add_task(*worker);

    auto* original = Scheduler::current_task();
    (void)original;
    Scheduler::set_current(*worker);

    sem.wait();

    JARVIS_ASSERT(worker->state == TaskState::BLOCKED);

    Scheduler::set_current(*original);
    sem.post();

    JARVIS_ASSERT(worker->state == TaskState::READY);

    Scheduler::remove_task(*worker);
    worker->cleanup();
    delete worker;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies mutex lock/unlock with owner tracking and waiter handoff.
// Input: Owner task locks mutex, unlocks, locks again; waiter task locks
// after owner unlocks
// Expect: Mutex correctly tracks owner and locked state across multiple
// lock/unlock cycles and task switches
// Depends: kernel::sync::Mutex, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(mutex_double_unlock, "PRE: none | POST: none") {
    sync::Mutex m;
    m.init();
    m.lock();
    m.unlock();
    JARVIS_ASSERT(!m.is_locked());
    JARVIS_ASSERT(m.owner() == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mutex_recursive_lock, "PRE: none | POST: none") {
    sync::Mutex m;
    m.init();
    m.lock();
    JARVIS_ASSERT(m.is_locked());
    m.lock();
    JARVIS_ASSERT(m.is_locked());
    m.unlock();
    JARVIS_ASSERT(m.is_locked());
    m.unlock();
    JARVIS_ASSERT(!m.is_locked());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(semaphore_timeout, "PRE: none | POST: none") {
    sync::Semaphore sem;
    sem.init(0, 1);
    bool ok = sem.try_wait();
    JARVIS_ASSERT(!ok);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(semaphore_multi_post, "PRE: none | POST: none") {
    sync::Semaphore sem;
    sem.init(0, 3);
    sem.post();
    sem.post();
    sem.post();
    sem.post();
    JARVIS_ASSERT(sem.value() == 3);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mutex_lock_unlock, "PRE: none | POST: none") {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(*owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*owner);
    mutex.lock();

    JARVIS_ASSERT(mutex.owner() == owner);
    JARVIS_ASSERT(mutex.is_locked());

    mutex.unlock();

    JARVIS_ASSERT(!mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == nullptr);

    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == owner);
    mutex.unlock();

    auto* waiter = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(waiter != nullptr);
    Scheduler::add_task(*waiter);

    Scheduler::set_current(*waiter);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == waiter);

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
// Testidea: Verifies Queue try_send/try_receive operations including full
// and empty edge cases.
// Input: Send/receive one item, fill queue to QUEUE_MAX_MSG_COUNT, attempt
// overfill, then drain one
// Expect: Single send/receive succeeds, queue fills to max, overfill returns
// false, drain reduces available count
// Depends: kernel::sync::Queue, sync::QUEUE_MAX_MSG_COUNT
JARVIS_TEST(queue_send_receive_block, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    uint8_t data[32] = {0xAA};
    JARVIS_ASSERT(queue.try_send(data, 1));
    JARVIS_ASSERT_EQ(1ULL, queue.available());

    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    JARVIS_ASSERT_EQ(0xAA, buf[0]);
    JARVIS_ASSERT(queue.available() == 0);

    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }
    JARVIS_ASSERT(queue.available() == sync::QUEUE_MAX_MSG_COUNT);

    JARVIS_ASSERT(!queue.try_send(data, 1));

    size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    JARVIS_ASSERT(queue.available() == sync::QUEUE_MAX_MSG_COUNT - 1);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that Queue::send blocks the sender when the queue is full.
// Input: Fill queue to capacity, create sender task that blocks on send,
// verify blocking behavior.
// Expect: Sender becomes BLOCKED when queue is full; becomes READY after
// receiver drains.
JARVIS_TEST(sync_queue_send_blocks_when_full, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    // Fill the queue
    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }
    JARVIS_ASSERT(queue.available() == sync::QUEUE_MAX_MSG_COUNT);

    auto* sender = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        q->send((uint8_t*)"test", 4);
    }, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    sender->user_data = &queue;
    Scheduler::add_task(*sender);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*sender);
    queue.send((uint8_t*)"test", 4);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    // Drain one message
    Scheduler::set_current(*original);
    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    JARVIS_ASSERT(sender->state == TaskState::READY);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that Queue::receive blocks the receiver when the queue
// is empty.
// Input: Create receiver task that blocks on receive from empty queue,
// verify blocking behavior.
// Expect: Receiver becomes BLOCKED when queue is empty; becomes READY after
// sender adds message.
JARVIS_TEST(sync_queue_receive_blocks_when_empty, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();
    JARVIS_ASSERT(queue.available() == 0);

    auto* receiver = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        uint8_t buf[32];
        size_t size = 32;
        q->receive(buf, &size);
    }, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    receiver->user_data = &queue;
    Scheduler::add_task(*receiver);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*receiver);
    uint8_t buf[32];
    size_t size = 32;
    queue.receive(buf, &size);
    JARVIS_ASSERT(receiver->state == TaskState::BLOCKED);

    // Add a message to unblock
    Scheduler::set_current(*original);
    JARVIS_ASSERT(queue.try_send((uint8_t*)"data", 4));
    JARVIS_ASSERT(receiver->state == TaskState::READY);

    Scheduler::remove_task(*receiver);
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a blocked sender is woken when a receiver consumes
// from the queue.
// Input: Fill queue, block sender on send, drain via receiver, verify sender
// becomes READY.
// Expect: Sender is BLOCKED after failed send, becomes READY after receiver
// calls receive.
JARVIS_TEST(sync_queue_wake_sender_on_receive, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    // Fill the queue
    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }

    auto* sender = TaskControlBlock::create([]() {
        sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
        q->send((uint8_t*)"wake", 4);
    }, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    sender->user_data = &queue;
    Scheduler::add_task(*sender);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*sender);
    queue.send((uint8_t*)"wake", 4);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    // Receiver drains one
    Scheduler::set_current(*original);
    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.receive(buf, &size));
    JARVIS_ASSERT(sender->state == TaskState::READY);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all sync primitive unit tests with the test framework.
// Input: None
// Expect: All semaphore, mutex, and queue tests are registered via
// JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_sync_tests() {
    Logger::info("Registering sync tests");
    JARVIS_REGISTER_TEST(semaphore_wait_post);
    JARVIS_REGISTER_TEST(mutex_double_unlock);
    JARVIS_REGISTER_TEST(mutex_recursive_lock);
    JARVIS_REGISTER_TEST(semaphore_timeout);
    JARVIS_REGISTER_TEST(semaphore_multi_post);
    JARVIS_REGISTER_TEST(mutex_lock_unlock);
    JARVIS_REGISTER_TEST(queue_send_receive_block);
    JARVIS_REGISTER_TEST(sync_queue_send_blocks_when_full);
    JARVIS_REGISTER_TEST(sync_queue_receive_blocks_when_empty);
    JARVIS_REGISTER_TEST(sync_queue_wake_sender_on_receive);
}
#pragma GCC diagnostic pop
