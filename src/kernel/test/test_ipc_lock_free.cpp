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

/// @file test_ipc_lock_free.cpp
/// @brief IPC lock-free queue tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): every kernel task is a REAL task
/// (prio ≥ 11) dispatched by the real timer ISR that calls IPC::send/recv in
/// its own running context.  The interrupt-flag checks are performed inside
/// the genuinely-running task; the ping-pong throughput runs on real ticks.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/ipc.hpp>
#include "test_sched_helpers.hpp"

using namespace kernel;

static volatile uint64_t g_ipc_recv_count_ = 0;

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
// Testidea: Kernel-task sys_receive does not call cli().  A REAL kernel task
// sends a message to itself and receives it; interrupts must remain enabled
// before, during, and after the receive.
// Input: Dispatched kernel task (prio 11) self-sends + self-recvs, sampling
//        the interrupt flag inside its own running context.
// Expect: interrupts enabled before/during/after receive; recv completes.
// Depends: IPC, Scheduler, arch::interrupts_enabled
JARVIS_TEST(ipc_recv_no_cli, "PRE: none | POST: none") {
    static uint64_t g_if_before = 0;
    static uint64_t g_if_during = 0;
    static uint64_t g_if_after = 0;
    static uint64_t g_ok = 0;

    auto *task = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            g_if_before = arch::interrupts_enabled() ? 1 : 0;

            Message msg{};
            msg.sender_id = self->id;
            msg.type = 42;
            msg.priority = 0;
            msg.data_size = 0;
            bool ok = IPC::send(self->id, msg);
            if (!ok)
                return;

            Message out;
            g_if_during = arch::interrupts_enabled() ? 1 : 0;
            ok = IPC::recv(out);
            g_if_after = arch::interrupts_enabled() ? 1 : 0;
            if (ok && out.type == 42)
                g_ok = 1;
        },
        11, 10);
    JARVIS_ASSERT(task != nullptr);
    JARVIS_ASSERT(task->page_table_ == 0);
    Scheduler::add_task(*task);
    Scheduler::reschedule();
    while (task->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT_EQ(1ULL, g_if_before);
    JARVIS_ASSERT_EQ(1ULL, g_if_during);
    JARVIS_ASSERT_EQ(1ULL, g_if_after);
    JARVIS_ASSERT_EQ(1ULL, g_ok);

    release_task(task);
    JARVIS_ASSERT(arch::interrupts_enabled());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel-task send_sync does not call cli().  A REAL kernel sender
// calls send_sync to a REAL kernel receiver that replies; interrupts must
// remain enabled throughout.
// Input: Dispatched sender (prio 12) + receiver (prio 11); the sender calls
//        IPC::send_sync, the receiver replies; both run for real.
// Expect: interrupts enabled before, during, and after send_sync.
// Depends: IPC, Scheduler, arch::interrupts_enabled
JARVIS_TEST(ipc_send_sync_no_cli, "PRE: none | POST: none") {
    static uint64_t g_receiver_id = 0;
    static uint64_t g_if_before = 0;
    static uint64_t g_if_during = 0;
    static uint64_t g_if_after = 0;
    static uint64_t g_reply_ok = 0;

    auto *receiver = TaskControlBlock::create(
        []() {
            Message msg;
            bool ok = false;
            for (int i = 0; i < 100000 && !ok; ++i)
                ok = IPC::recv(msg);
            if (!ok)
                return;
            Message reply;
            reply.sender_id = Scheduler::current_task()->id;
            reply.type = 99;
            reply.priority = 0;
            reply.data_size = 0;
            IPC::send(msg.sender_id, reply);
        },
        11, 10);
    JARVIS_ASSERT(receiver != nullptr);
    g_receiver_id = receiver->id;

    auto *sender = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            g_if_before = arch::interrupts_enabled() ? 1 : 0;
            Message msg;
            msg.sender_id = self->id;
            msg.type = 42;
            msg.priority = 0;
            msg.data_size = 0;
            Message reply;
            g_if_during = arch::interrupts_enabled() ? 1 : 0;
            bool ok = IPC::send_sync(g_receiver_id, msg, reply);
            g_if_after = arch::interrupts_enabled() ? 1 : 0;
            if (ok && reply.type == 99)
                g_reply_ok = 1;
        },
        12, 10);
    JARVIS_ASSERT(sender != nullptr);
    JARVIS_ASSERT(sender->page_table_ == 0);

    {
        arch::IrqGuard _guard;
        Scheduler::add_task(*sender);
        Scheduler::add_task(*receiver);
    }
    Scheduler::reschedule();

    while (sender->state != TaskState::TERMINATED ||
           receiver->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT_EQ(1ULL, g_if_before);
    JARVIS_ASSERT_EQ(1ULL, g_if_during);
    JARVIS_ASSERT_EQ(1ULL, g_if_after);
    JARVIS_ASSERT_EQ(1ULL, g_reply_ok);

    release_task(sender);
    release_task(receiver);
    JARVIS_ASSERT(arch::interrupts_enabled());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Measure IPC roundtrip throughput with lock-free primitives over
// real timer ticks.  Two REAL kernel tasks ping-pong; every roundtrip
// completes through genuine dispatch.
// Input: 200 IPC roundtrips between two kernel tasks (ping-pong) driven by
//        real ticks.
// Expect: All roundtrips complete; no deadlock; both tasks terminate.
// Depends: IPC, Scheduler
JARVIS_TEST(ipc_lock_free_throughput, "PRE: none | POST: none") {
    static uint64_t g_a_id = 0;
    static uint64_t g_b_id = 0;
    static uint64_t g_a_done = 0;
    static uint64_t g_b_done = 0;
    static uint64_t g_a_ok = 0;
    static uint64_t g_b_ok = 0;

    // A: receives from B, replies; B: receives from A, replies.  Each does
    // ROUNDS iterations of genuine IPC::recv/send.
    auto *task_a = TaskControlBlock::create(
        []() {
            for (uint64_t i = 0; i < 100; ++i) {
                Message msg;
                bool ok = false;
                for (int k = 0; k < 100000 && !ok; ++k)
                    ok = IPC::recv(msg);
                if (!ok) {
                    g_a_ok = 1;
                    return;
                }
                Message reply;
                reply.sender_id = Scheduler::current_task()->id;
                reply.type = msg.type + 1;
                reply.priority = 0;
                reply.data_size = 0;
                if (!IPC::send(g_b_id, reply)) {
                    g_a_ok = 1;
                    return;
                }
            }
            g_a_done = 1;
        },
        11, 10);

    auto *task_b = TaskControlBlock::create(
        []() {
            // Seed: A sends the first message to B.
            Message seed;
            seed.sender_id = Scheduler::current_task()->id;
            seed.type = 0;
            seed.priority = 0;
            seed.data_size = 0;
            if (!IPC::send(g_a_id, seed)) {
                g_b_ok = 1;
                return;
            }
            for (uint64_t i = 0; i < 100; ++i) {
                Message msg;
                bool ok = false;
                for (int k = 0; k < 100000 && !ok; ++k)
                    ok = IPC::recv(msg);
                if (!ok) {
                    g_b_ok = 1;
                    return;
                }
                Message reply;
                reply.sender_id = Scheduler::current_task()->id;
                reply.type = msg.type + 1;
                reply.priority = 0;
                reply.data_size = 0;
                if (!IPC::send(g_a_id, reply)) {
                    g_b_ok = 1;
                    return;
                }
            }
            g_b_done = 1;
        },
        12, 10);
    JARVIS_ASSERT(task_a != nullptr);
    JARVIS_ASSERT(task_b != nullptr);
    g_a_id = task_a->id;
    g_b_id = task_b->id;

    {
        arch::IrqGuard _guard;
        Scheduler::add_task(*task_a);
        Scheduler::add_task(*task_b);
    }
    // Drive the ping-pong on real ticks.
    uint64_t start = arch::Timer::ticks();
    while ((!g_a_done || !g_b_done) &&
           (arch::Timer::ticks() - start) < 5000) {
        Scheduler::reschedule();
        asm volatile("pause");
    }

    JARVIS_ASSERT_EQ(1ULL, g_a_done);
    JARVIS_ASSERT_EQ(1ULL, g_b_done);
    JARVIS_ASSERT_EQ(0ULL, g_a_ok);
    JARVIS_ASSERT_EQ(0ULL, g_b_ok);

    release_task(task_a);
    release_task(task_b);
    JARVIS_TEST_PASS();
}

void register_ipc_lock_free_tests() {
    Logger::info("Registering IPC lock-free tests");
    JARVIS_REGISTER_TEST(ipc_recv_no_cli);
    JARVIS_REGISTER_TEST(ipc_send_sync_no_cli);
    JARVIS_REGISTER_TEST(ipc_lock_free_throughput);
}
