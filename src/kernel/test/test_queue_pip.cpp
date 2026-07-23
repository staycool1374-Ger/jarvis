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

/// @file test_queue_pip.cpp
/// @brief Priority Inheritance Protocol tests for sync::Queue.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// ---------------------------------------------------------------------------
// Queue PIP — boost sender when high-pri receiver blocks on empty queue
// ---------------------------------------------------------------------------
// Low-pri sender (5) sends, then high-pri receiver (15) blocks on empty
// queue.  Sender's priority should be boosted to the receiver's priority.
// NOTE: restore_sender() runs inside the blocked receiver's receive() when
// it completes, which is deferred until the next context switch.  We assert
// only the boost, not the deferred restore.
JARVIS_TEST(queue_pip_boost_sender, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    auto *low = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    auto *high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    // Low sends → last_sender_ = low
    uint8_t msg[4] = {1, 2, 3, 4};
    Scheduler::set_current(*low);
    JARVIS_ASSERT(queue.try_send(msg, 4));

    // Drain queue so receive will block
    uint8_t buf[32];
    size_t sz = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &sz));

    // High blocks on empty queue → boost_sender boosts low
    Scheduler::set_current(*high);
    queue.receive(buf, &sz);
    JARVIS_ASSERT(high->state == TaskState::BLOCKED);
    JARVIS_ASSERT_FMT(low->priority >= high->priority,
                      "low->priority=%lu high->priority=%lu",
                      low->priority, high->priority);

    // Low sends to unblock high
    Scheduler::set_current(*low);
    JARVIS_ASSERT(queue.try_send(msg, 4));
    JARVIS_ASSERT(high->state == TaskState::READY);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// Queue PIP — boost receiver when high-pri sender blocks on full queue
// ---------------------------------------------------------------------------
// Low-pri receiver (5) receives, then queue filled, high-pri sender (15)
// blocks on full queue.  Receiver's priority should be boosted to sender's.
JARVIS_TEST(queue_pip_boost_receiver, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    auto *low = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    auto *high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    // Seed a message so low's receive succeeds → last_receiver_ = low
    uint8_t seed[4] = {0};
    JARVIS_ASSERT(queue.try_send(seed, 1));

    uint8_t buf[32];
    size_t sz = 32;
    Scheduler::set_current(*low);
    JARVIS_ASSERT(queue.try_receive(buf, &sz));

    // Fill queue to capacity
    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[4] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }

    // High blocks on full queue → boost_receiver boosts low
    Scheduler::set_current(*high);
    queue.send((uint8_t *)"test", 4);
    JARVIS_ASSERT(high->state == TaskState::BLOCKED);
    JARVIS_ASSERT_FMT(low->priority >= high->priority,
                      "low->priority=%lu high->priority=%lu",
                      low->priority, high->priority);

    // Drain one → unblocks high
    sz = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &sz));
    JARVIS_ASSERT(high->state == TaskState::READY);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// Queue PIP — multiple senders, boost highest
// ---------------------------------------------------------------------------
// Two low-pri senders (5, 8) send.  Queue drained.  High-pri receiver (20)
// blocks on empty queue.  The last sender (prio 8) should be boosted.
JARVIS_TEST(queue_pip_multiple_senders, "PRE: none | POST: none") {
    sync::Queue queue;
    queue.init();

    auto *low1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(low1 != nullptr);
    low1->base_priority = 5;
    low1->priority = 5;
    Scheduler::add_task(*low1);

    auto *low2 = TaskControlBlock::create([]() {}, 8, 10);
    JARVIS_ASSERT(low2 != nullptr);
    low2->base_priority = 8;
    low2->priority = 8;
    Scheduler::add_task(*low2);

    uint8_t msg[4] = {0};
    Scheduler::set_current(*low1);
    JARVIS_ASSERT(queue.try_send(msg, 4));
    Scheduler::set_current(*low2);
    JARVIS_ASSERT(queue.try_send(msg, 4));

    // Drain queue
    uint8_t buf[32];
    size_t sz = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &sz));
    JARVIS_ASSERT(queue.try_receive(buf, &sz));

    // High blocks on empty queue → boost_sender boosts last sender (low2)
    auto *high = TaskControlBlock::create([]() {}, 20, 10);
    JARVIS_ASSERT(high != nullptr);
    high->base_priority = 20;
    high->priority = 20;
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    queue.receive(buf, &sz);
    JARVIS_ASSERT(high->state == TaskState::BLOCKED);
    JARVIS_ASSERT_FMT(low2->priority >= high->priority,
                      "low2->priority=%lu high->priority=%lu",
                      low2->priority, high->priority);

    // Send to unblock
    Scheduler::set_current(*low1);
    JARVIS_ASSERT(queue.try_send(msg, 4));
    JARVIS_ASSERT(high->state == TaskState::READY);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low2);
    low2->cleanup();
    delete low2;
    Scheduler::remove_task(*low1);
    low1->cleanup();
    delete low1;
    JARVIS_TEST_PASS();
}

void register_queue_pip_tests() {
    Logger::info("Registering Queue PIP tests");
    JARVIS_REGISTER_TEST(queue_pip_boost_sender);
    JARVIS_REGISTER_TEST(queue_pip_boost_receiver);
    JARVIS_REGISTER_TEST(queue_pip_multiple_senders);
}
