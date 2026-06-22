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
#include <kernel/task/sporadic_server.hpp>

using namespace kernel;
using task::SporadicServer;

// Runmode: kernel
// Testidea: Verify init sets budget, period, and background priority correctly.
// Expect: All accessors return the configured values; state is IDLE.
JARVIS_TEST(sporadic_server_init) {
    SporadicServer ss;
    ss.init(10, 100, 42);
    JARVIS_ASSERT_EQ(10ULL, ss.max_budget());
    JARVIS_ASSERT_EQ(100ULL, ss.period());
    JARVIS_ASSERT_EQ(10ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(42ULL, ss.current_priority());
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT(!ss.is_active());
    JARVIS_ASSERT(ss.has_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Activation transitions IDLE -> ACTIVE and records activation time.
// Expect: state is ACTIVE, is_active returns true, budget unchanged.
JARVIS_TEST(sporadic_server_activation) {
    SporadicServer ss;
    ss.init(10, 100, 42);
    ss.on_activation(50);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT(ss.is_active());
    JARVIS_ASSERT_EQ(10ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Double activation while already active is a no-op.
// Expect: State remains ACTIVE; budget same as after first activation.
JARVIS_TEST(sporadic_server_double_activation) {
    SporadicServer ss;
    ss.init(10, 100, 42);
    ss.on_activation(50);
    uint64_t budget_after_first = ss.remaining_budget();
    // Second activation while already active — should be no-op
    ss.on_activation(60);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT_EQ(budget_after_first, ss.remaining_budget());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Consume decrements budget each tick; returns false when exhausted.
// Expect: After C ticks of consume, budget is 0, state is EXHAUSTED,
//         and a replenishment event is scheduled.
JARVIS_TEST(sporadic_server_consumption_exhaustion) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.on_activation(10);

    for (uint64_t i = 0; i < 4; i++) {
        JARVIS_ASSERT(ss.consume(10 + i));
        JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    }
    // 5th consume exhausts budget
    bool ok = ss.consume(14);
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(0ULL, ss.remaining_budget());
    JARVIS_ASSERT(!ss.has_budget());
    // Should have one pending replenishment at t=10+100=110, amount=5
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());
    // Priority should now be background
    JARVIS_ASSERT_EQ(42ULL, ss.current_priority());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Completion schedules a replenishment for consumed budget and goes IDLE.
// Expect: After consuming 3 ticks and completing, 1 replenishment pending,
//         state IDLE, remaining budget = 7 (C - consumed).
JARVIS_TEST(sporadic_server_completion_schedules_replenishment) {
    SporadicServer ss;
    ss.init(10, 100, 42);
    ss.on_activation(20);

    ss.consume(20);
    ss.consume(21);
    ss.consume(22);

    ss.on_completion(23);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT(!ss.is_active());
    JARVIS_ASSERT_EQ(7ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Processing due replenishment restores budget and reactivates server.
// Expect: After replenishment at t=110 fires at tick 110, budget is restored
//         to 5, state transitions back to ACTIVE, pending count is 0.
JARVIS_TEST(sporadic_server_replenishment_restores_budget) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.on_activation(10);

    // Exhaust all 5 ticks
    for (uint64_t i = 0; i < 5; i++)
        ss.consume(10 + i);

    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(0ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());

    // Fast-forward to replenishment time
    ss.process_replenishments(109);
    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(0ULL, ss.remaining_budget());

    ss.process_replenishments(110);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());
    JARVIS_ASSERT(ss.has_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Multiple activations and completions produce correct replenishment
//           chain (one replenishment per busy interval).
// Expect: Two complete cycles: after first, replenishment at t=110 amount=5;
//         after second, replenishment at t=210 amount=5.
JARVIS_TEST(sporadic_server_two_cycles) {
    SporadicServer ss;
    ss.init(5, 100, 42);

    // ---- Cycle 1 ----
    ss.on_activation(10);
    for (uint64_t i = 0; i < 5; i++)
        ss.consume(10 + i);
    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(0ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());

    // Replenishment fires at 110
    ss.process_replenishments(110);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());

    // ---- Cycle 2 ----
    // Server is already active from replenishment; a new activation while
    // active is a no-op, which is correct — the job is already running.
    ss.on_activation(115);  // no-op (already ACTIVE)
    for (uint64_t i = 0; i < 5; i++)
        ss.consume(110 + i);
    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(0ULL, ss.remaining_budget());
    // Should have replenishment at activation_time (110) + T (100) = 210
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());

    ss.process_replenishments(210);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Replenishment budget is capped at C even if multiple events fire.
// Expect: If two replenishments each of amount 5 fire at the same tick,
//         budget does not exceed C = 8.
JARVIS_TEST(sporadic_server_budget_capped_at_C) {
    SporadicServer ss;
    ss.init(8, 50, 42);
    ss.on_activation(10);

    // Consume 3 ticks then complete — schedule repl at 60 for amount 3
    ss.consume(10); ss.consume(11); ss.consume(12);
    ss.on_completion(13);
    JARVIS_ASSERT_EQ(1ULL, ss.pending_count());

    // Re-activate at 20 (budget still 5)
    ss.on_activation(20);
    ss.consume(20); ss.consume(21); ss.consume(22);
    ss.on_completion(23);
    JARVIS_ASSERT_EQ(2ULL, ss.pending_count());

    // Process replenishments — both fire at/after 60
    ss.process_replenishments(60);
    // First repl: budget 5 + 3 = 8 (capped at 8)
    // Second repl: would add 3 more but capped at C=8
    JARVIS_ASSERT_EQ(8ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Completion when no budget was consumed schedules no replenishment.
// Expect: After activation -> immediate completion (no consume), 0 pending.
JARVIS_TEST(sporadic_server_completion_no_consume) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.on_activation(10);
    ss.on_completion(10);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Consuming when IDLE returns false and does not change state.
// Expect: consume() on idle server returns false, state stays IDLE.
JARVIS_TEST(sporadic_server_consume_idle) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    bool ok = ss.consume(10);
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Completion when already IDLE is a no-op.
// Expect: on_completion() called twice has no effect.
JARVIS_TEST(sporadic_server_double_completion) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.on_activation(10);
    ss.consume(10); ss.consume(11);
    ss.on_completion(12);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    uint64_t budget = ss.remaining_budget();
    // Second completion while idle
    ss.on_completion(13);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT_EQ(budget, ss.remaining_budget());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Process replenishments with empty queue is a safe no-op.
// Expect: No crash, state unchanged, budget unchanged.
JARVIS_TEST(sporadic_server_process_empty_queue) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.process_replenishments(999);
    JARVIS_ASSERT(ss.state() == SporadicServer::IDLE);
    JARVIS_ASSERT_EQ(5ULL, ss.remaining_budget());
    JARVIS_ASSERT_EQ(0ULL, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Replenishment queue handles up to MAX_REPLENISHMENTS entries.
// Expect: Insertions succeed until full; further insertions return false.
JARVIS_TEST(sporadic_server_queue_limits) {
    SporadicServer ss;
    ss.init(10, 10, 42);

    // Each activation+completion adds one replenishment
    for (uint64_t i = 0; i < SporadicServer::MAX_REPLENISHMENTS; i++) {
        ss.on_activation(10 * i);
        ss.consume(10 * i);
        ss.on_completion(10 * i + 1);
    }
    JARVIS_ASSERT_EQ(SporadicServer::MAX_REPLENISHMENTS, ss.pending_count());

    // Next one should fail silently
    ss.on_activation(1000);
    ss.consume(1000);
    ss.on_completion(1001);
    JARVIS_ASSERT_EQ(SporadicServer::MAX_REPLENISHMENTS, ss.pending_count());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Priority after exhaustion returns bg_priority; after replenishment
//           and reactivation, priority returns to base_priority (set externally).
// Expect: When EXHAUSTED, current_priority == bg_priority (42).
//         Set base_priority externally, after replenishment fires, current_priority == base.
JARVIS_TEST(sporadic_server_priority_transitions) {
    SporadicServer ss;
    ss.init(5, 100, 42);
    ss.set_base_priority(1);

    ss.on_activation(10);
    for (uint64_t i = 0; i < 5; i++)
        ss.consume(10 + i);
    JARVIS_ASSERT(ss.state() == SporadicServer::EXHAUSTED);
    JARVIS_ASSERT_EQ(42ULL, ss.current_priority());

    ss.process_replenishments(110);
    JARVIS_ASSERT(ss.state() == SporadicServer::ACTIVE);
    JARVIS_ASSERT_EQ(1ULL, ss.current_priority());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all SporadicServer unit tests with the test framework.
void register_sporadic_server_tests() {
    Logger::info("Registering SporadicServer tests");
    JARVIS_REGISTER_TEST(sporadic_server_init);
    JARVIS_REGISTER_TEST(sporadic_server_activation);
    JARVIS_REGISTER_TEST(sporadic_server_double_activation);
    JARVIS_REGISTER_TEST(sporadic_server_consumption_exhaustion);
    JARVIS_REGISTER_TEST(sporadic_server_completion_schedules_replenishment);
    JARVIS_REGISTER_TEST(sporadic_server_replenishment_restores_budget);
    JARVIS_REGISTER_TEST(sporadic_server_two_cycles);
    JARVIS_REGISTER_TEST(sporadic_server_budget_capped_at_C);
    JARVIS_REGISTER_TEST(sporadic_server_completion_no_consume);
    JARVIS_REGISTER_TEST(sporadic_server_consume_idle);
    JARVIS_REGISTER_TEST(sporadic_server_double_completion);
    JARVIS_REGISTER_TEST(sporadic_server_process_empty_queue);
    JARVIS_REGISTER_TEST(sporadic_server_queue_limits);
    JARVIS_REGISTER_TEST(sporadic_server_priority_transitions);
}