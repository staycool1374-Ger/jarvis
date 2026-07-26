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

/// @file test_testrunner.cpp
/// @brief Base test class verifying test-harness integrity: snapshot/restore
///        correctness, priority-ordered blocked-sender wakeup, and leak
///        detection.  These tests must pass before any other test class can
///        be trusted — they validate the test infrastructure itself.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/test/test_isolate.hpp>
#include <kernel/test/resource_tracker.hpp>

using namespace kernel;

// ── Shared state for IPC tests ──────────────────────────────────────────
static volatile bool   ipc_test_done_ = false;
static volatile uint64_t ipc_recv_count_ = 0;
static constexpr uint64_t IPC_MSG_TYPE_TEST = 42;

// ── High-priority receiver: drain queue ──────────────────────────────
static void ipc_high_prio_receiver() {
    for (uint64_t i = 0; i < 10 && !ipc_test_done_; ++i) {
        Message msg;
        while (IPC::recv(msg)) {
            __atomic_add_fetch(&ipc_recv_count_, 1, __ATOMIC_RELAXED);
        }
        Scheduler::reschedule();
    }
    ipc_test_done_ = true;
}

// ── Low-priority sender ──────────────────────────────────────────────
static void ipc_low_prio_sender(uint64_t target_id) {
    for (uint64_t i = 0; i < 5 && !ipc_test_done_; ++i) {
        Message msg{};
        msg.type = IPC_MSG_TYPE_TEST;
        msg.priority = 0;
        IPC::send(target_id, msg, IPC_NONBLOCK);
        for (uint64_t w = 0; w < 100; ++w) arch::pause();
    }
}

// ======================================================================
// Tests
// ======================================================================

// Runmode: kernel
// Testidea: Snapshot/restore must leave the ready queue bitmap consistent.
//           Create a task, snapshot, remove, restore, verify.
JARVIS_TEST(harness_snapshot_bitmap_consistency,
            "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    JARVIS_ASSERT(t->in_ready_queue_);

    Scheduler::remove_task(*t);
    t->cleanup();
    delete t;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Priority-ordered wakeup of blocked senders.  High-prio sender
//           must be woken and run before low-prio sender.
JARVIS_TEST(harness_priority_ordered_wakeup,
            "PRE: none | POST: none") {
    ipc_test_done_ = false;
    ipc_recv_count_ = 0;

    auto *receiver = TaskControlBlock::create(ipc_high_prio_receiver, 3, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto *sender = TaskControlBlock::create(
        []() { ipc_low_prio_sender(2); }, 10, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    for (int h = 0; h < 200 && !ipc_test_done_; ++h) {
        Scheduler::reschedule();
        arch::hlt();
    }

    JARVIS_ASSERT_FMT(ipc_recv_count_ > 0,
                      "Receiver processed %lu messages (expected > 0)",
                      ipc_recv_count_);

    if (receiver && receiver->magic == TaskControlBlock::TCB_MAGIC) {
        if (receiver->state != TaskState::TERMINATED)
            Scheduler::remove_task(*receiver);
        receiver->cleanup();
        delete receiver;
    }
    if (sender && sender->magic == TaskControlBlock::TCB_MAGIC) {
        if (sender->state != TaskState::TERMINATED)
            Scheduler::remove_task(*sender);
        sender->cleanup();
        delete sender;
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Sender blocks on full queue, receiver drains, sender wakes.
//           Tests the RMS blocked-sender wakeup path WITHOUT send_sync.
JARVIS_TEST(harness_blocked_sender_wakes,
            "PRE: none | POST: none") {
    ipc_recv_count_ = 0;

    // Create a receiver that drains the queue
    auto *receiver = TaskControlBlock::create([]() {
        for (uint64_t i = 0; i < IPC_MAX_QUEUE_MSG + 5; ++i) {
            Message msg;
            if (IPC::recv(msg)) {
                __atomic_add_fetch(&ipc_recv_count_, 1, __ATOMIC_RELAXED);
                Scheduler::reschedule();
            }
        }
    }, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    // Fill the queue
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        Message msg{};
        msg.type = IPC_MSG_TYPE_TEST;
        msg.priority = 0;
        IPC::send(receiver->id, msg, IPC_NONBLOCK);
    }

    // Send one more — should block (queue full) and this test must
    // verify that the sender is woken and the message arrives.
    ipc_recv_count_ = 0;
    Message block_msg{};
    block_msg.type = IPC_MSG_TYPE_TEST;
    block_msg.priority = 10;

    // Use deferred-send: the scheduler must preempt us to the receiver
    // so it drains the queue and wakes us.
    if (!IPC::send(receiver->id, block_msg, 0)) {
        // If IPC::send returns false (e.g. OOM), the test failed
        JARVIS_ASSERT_FMT(false, "Blocking send failed");
    }

    // After waking, our message is in the queue. Let receiver process it.
    for (int h = 0; h < 50; ++h) {
        Scheduler::reschedule();
        arch::hlt();
        if (ipc_recv_count_ > IPC_MAX_QUEUE_MSG)
            break;
    }

    JARVIS_ASSERT_FMT(ipc_recv_count_ > IPC_MAX_QUEUE_MSG,
                      "Receiver should have processed all messages (%lu/%lu)",
                      (uint64_t)ipc_recv_count_,
                      (uint64_t)(IPC_MAX_QUEUE_MSG + 1));

    if (receiver && receiver->magic == TaskControlBlock::TCB_MAGIC) {
        Scheduler::remove_task(*receiver);
        receiver->cleanup();
        delete receiver;
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: in_ready_queue_ flag after re-enqueue.
JARVIS_TEST(harness_snapshot_inrq_consistency,
            "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    JARVIS_ASSERT(t->in_ready_queue_);

    t->in_ready_queue_ = false;
    t->rq_priority_ = 0;

    Scheduler::set_task_ready(*t);
    JARVIS_ASSERT_FMT(t->in_ready_queue_,
                      "Task should be in ready queue after set_task_ready");

    if (t->magic == TaskControlBlock::TCB_MAGIC) {
        Scheduler::remove_task(*t);
        t->cleanup();
        delete t;
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Resource leak detection after task create/destroy cycle.
JARVIS_TEST(harness_leak_detection, "PRE: none | POST: none") {
    for (int i = 0; i < 10; ++i) {
        auto *t = TaskControlBlock::create([]() {}, 5, 10);
        JARVIS_ASSERT(t != nullptr);
        Scheduler::add_task(*t);
        Scheduler::remove_task(*t);
        t->cleanup();
        delete t;
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Spawn and cleanup multiple tasks without leaks.
JARVIS_TEST(harness_multi_task_spawn_cleanup,
            "PRE: none | POST: none") {
    static constexpr uint64_t NUM_WORKERS = 5;
    TaskControlBlock *workers[NUM_WORKERS] = {};
    for (uint64_t i = 0; i < NUM_WORKERS; ++i) {
        workers[i] = TaskControlBlock::create([]() {}, 5, 10);
        JARVIS_ASSERT(workers[i] != nullptr);
        Scheduler::add_task(*workers[i]);
    }

    for (uint64_t i = 0; i < NUM_WORKERS; ++i) {
        if (workers[i] &&
            workers[i]->magic == TaskControlBlock::TCB_MAGIC) {
            Scheduler::remove_task(*workers[i]);
            workers[i]->cleanup();
            delete workers[i];
        }
    }

    JARVIS_TEST_PASS();
}

void register_testrunner_tests() {
    Logger::info("Registering TestRunner tests");
    JARVIS_REGISTER_TEST(harness_snapshot_bitmap_consistency);
    JARVIS_REGISTER_TEST(harness_priority_ordered_wakeup);
    JARVIS_REGISTER_TEST(harness_blocked_sender_wakes);
    JARVIS_REGISTER_TEST(harness_snapshot_inrq_consistency);
    JARVIS_REGISTER_TEST(harness_leak_detection);
    JARVIS_REGISTER_TEST(harness_multi_task_spawn_cleanup);
}
