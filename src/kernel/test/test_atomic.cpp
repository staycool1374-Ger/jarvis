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
#include <lib/atomic.hpp>
#include <constants.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/io.hpp>

using namespace kernel;

// ============================================================
// Test 1: Basic atomic operations (single-threaded)
// ============================================================

JARVIS_TEST(atomic_load_store) {
    uint32_t val = 42;
    kernel::atomic_store(&val, uint32_t(100));
    JARVIS_ASSERT_EQ(100U, kernel::atomic_load(&val));
    kernel::atomic_store(&val, uint32_t(0));
    JARVIS_ASSERT_EQ(0U, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_exchange) {
    uint32_t val = 42;
    uint32_t old = kernel::atomic_exchange(&val, uint32_t(99));
    JARVIS_ASSERT_EQ(42U, old);
    JARVIS_ASSERT_EQ(99U, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_compare_exchange) {
    uint32_t val = 42;
    uint32_t expected = 42;
    bool ok = kernel::atomic_compare_exchange(&val, &expected, uint32_t(99));
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ(99U, kernel::atomic_load(&val));
    expected = 42;
    ok = kernel::atomic_compare_exchange(&val, &expected, uint32_t(0));
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT_EQ(99U, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_fetch_add) {
    uint32_t val = 10;
    uint32_t prev = kernel::atomic_fetch_add(&val, uint32_t(5));
    JARVIS_ASSERT_EQ(10U, prev);
    JARVIS_ASSERT_EQ(15U, kernel::atomic_load(&val));
    prev = kernel::atomic_fetch_add(&val, uint32_t(1));
    JARVIS_ASSERT_EQ(15U, prev);
    JARVIS_ASSERT_EQ(16U, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_fetch_sub) {
    uint32_t val = 100;
    uint32_t prev = kernel::atomic_fetch_sub(&val, uint32_t(30));
    JARVIS_ASSERT_EQ(100U, prev);
    JARVIS_ASSERT_EQ(70U, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_fetch_and) {
    uint32_t val = 0xFF;
    uint32_t prev = kernel::atomic_fetch_and(&val, uint32_t(0x0F));
    JARVIS_ASSERT_EQ(0xFFU, prev);
    JARVIS_ASSERT_EQ(0x0FU, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_fetch_or) {
    uint32_t val = 0xF0;
    uint32_t prev = kernel::atomic_fetch_or(&val, uint32_t(0x0F));
    JARVIS_ASSERT_EQ(0xF0U, prev);
    JARVIS_ASSERT_EQ(0xFFU, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(atomic_64bit_operations) {
    uint64_t val = 0xAAAAAAAAULL;
    kernel::atomic_store(&val, uint64_t(0xBBBBBBBBBBBBBBBBULL));
    JARVIS_ASSERT_EQ(0xBBBBBBBBBBBBBBBBULL, kernel::atomic_load(&val));
    uint64_t old = kernel::atomic_exchange(&val, uint64_t(0xCCCCCCCCCCCCCCCCULL));
    JARVIS_ASSERT_EQ(0xBBBBBBBBBBBBBBBBULL, old);
    JARVIS_ASSERT_EQ(0xCCCCCCCCCCCCCCCCULL, kernel::atomic_load(&val));
    old = kernel::atomic_fetch_add(&val, uint64_t(1));
    JARVIS_ASSERT_EQ(0xCCCCCCCCCCCCCCCCULL, old);
    JARVIS_ASSERT_EQ(0xCCCCCCCCCCCCCCCDULL, kernel::atomic_load(&val));
    JARVIS_TEST_PASS();
}

// ============================================================
// Test 2: Store-buffering litmus test (SB)
// ============================================================
// Classic SB: two tasks each store to one variable and load from the other.
// Under sequential consistency, both seeing 0 is forbidden.
//   Initially: x = 0, y = 0
//   Task 1: x = 1; fence; r1 = y;
//   Task 2: y = 1; fence; r2 = x;
// Forbidden: r1 == 0 && r2 == 0

static volatile uint32_t* g_sb_x;
static volatile uint32_t* g_sb_y;
static uint32_t g_sb_r1;
static uint32_t g_sb_r2;

static void sb_worker_a() {
    kernel::atomic_store(const_cast<volatile uint32_t*>(g_sb_x), uint32_t(1));
    kernel::atomic_fence();
    g_sb_r1 = kernel::atomic_load(const_cast<volatile uint32_t*>(g_sb_y));
}

static void sb_worker_b() {
    kernel::atomic_store(const_cast<volatile uint32_t*>(g_sb_y), uint32_t(1));
    kernel::atomic_fence();
    g_sb_r2 = kernel::atomic_load(const_cast<volatile uint32_t*>(g_sb_x));
}

JARVIS_TEST(atomic_sb_litmus) {
    static constexpr uint64_t SB_ITER = 100;

    uint64_t x_phys = PMM::alloc_page();
    uint64_t y_phys = PMM::alloc_page();
    JARVIS_ASSERT(x_phys != 0 && y_phys != 0);
    auto page_guard = ScopeGuard([&]() {
        PMM::free_page(x_phys);
        PMM::free_page(y_phys);
    });

    g_sb_x = reinterpret_cast<volatile uint32_t*>(arch::HHDM_OFFSET + x_phys);
    g_sb_y = reinterpret_cast<volatile uint32_t*>(arch::HHDM_OFFSET + y_phys);

    uint64_t forbidden_count = 0;

    for (uint64_t iter = 0; iter < SB_ITER; ++iter) {
        *const_cast<volatile uint32_t*>(g_sb_x) = 0;
        *const_cast<volatile uint32_t*>(g_sb_y) = 0;
        kernel::atomic_fence();
        g_sb_r1 = 99;
        g_sb_r2 = 99;

        auto* task_a = TaskControlBlock::create(sb_worker_a, 5, 10);
        JARVIS_ASSERT(task_a != nullptr);
        Scheduler::add_task(*task_a);

        auto* task_b = TaskControlBlock::create(sb_worker_b, 5, 10);
        JARVIS_ASSERT(task_b != nullptr);
        Scheduler::add_task(*task_b);

        auto cleanup = ScopeGuard([&]() {
            Scheduler::remove_task(*task_a);
            task_a->cleanup();
            delete task_a;
            Scheduler::remove_task(*task_b);
            task_b->cleanup();
            delete task_b;
        });

        auto* original = Scheduler::current_task();
        for (int t = 0; t < 10; ++t) {
            if (task_a->state != TaskState::TERMINATED) {
                Scheduler::set_current(*task_a);
                Scheduler::reschedule();
            }
            if (task_b->state != TaskState::TERMINATED) {
                Scheduler::set_current(*task_b);
                Scheduler::reschedule();
            }
            Scheduler::on_tick();
        }
        Scheduler::set_current(*original);

        if (g_sb_r1 == 0 && g_sb_r2 == 0) {
            ++forbidden_count;
        }

        cleanup.dismiss();
        Scheduler::remove_task(*task_a);
        task_a->cleanup();
        delete task_a;
        Scheduler::remove_task(*task_b);
        task_b->cleanup();
        delete task_b;
    }

    JARVIS_ASSERT_FMT(forbidden_count == 0,
        "SB litmus: forbidden (r1==0 && r2==0) occurred %lu/%lu times",
        forbidden_count, SB_ITER);
    JARVIS_TEST_PASS();
}

// ============================================================
// Test 3: Message-passing litmus test (MP)
// ============================================================
// Under SC, r1 must be 42 after data+flag sync.
//   Initially: flag = 0, data = 0
//   Task 1: data = 42; fence; flag = 1;
//   Task 2: while (flag == 0); fence; r1 = data;
// Outcome: r1 == 42 (data must be visible)

static volatile uint64_t* g_mp_flag;
static uint64_t* g_mp_data;
static uint64_t g_mp_observed;

static void mp_producer() {
    kernel::atomic_store(g_mp_data, uint64_t(42));
    kernel::atomic_fence();
    kernel::atomic_store(const_cast<volatile uint64_t*>(g_mp_flag), uint64_t(1));
}

static void mp_consumer() {
    while (kernel::atomic_load(const_cast<volatile uint64_t*>(g_mp_flag)) == 0) {
        arch::pause();
    }
    kernel::atomic_fence();
    g_mp_observed = kernel::atomic_load(g_mp_data);
}

JARVIS_TEST(atomic_mp_litmus) {
    uint64_t flag_phys = PMM::alloc_page();
    uint64_t data_phys = PMM::alloc_page();
    JARVIS_ASSERT(flag_phys != 0 && data_phys != 0);

    auto page_guard = ScopeGuard([&]() {
        PMM::free_page(flag_phys);
        PMM::free_page(data_phys);
    });

    g_mp_flag = reinterpret_cast<volatile uint64_t*>(arch::HHDM_OFFSET + flag_phys);
    g_mp_data = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + data_phys);
    *const_cast<volatile uint64_t*>(g_mp_flag) = 0;
    *g_mp_data = 0;
    kernel::atomic_fence();
    g_mp_observed = 0;

    auto* producer = TaskControlBlock::create(mp_producer, 5, 10);
    JARVIS_ASSERT(producer != nullptr);
    Scheduler::add_task(*producer);

    auto* consumer = TaskControlBlock::create(mp_consumer, 5, 10);
    JARVIS_ASSERT(consumer != nullptr);
    Scheduler::add_task(*consumer);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*producer);
        producer->cleanup();
        delete producer;
        Scheduler::remove_task(*consumer);
        consumer->cleanup();
        delete consumer;
    });

    auto* original = Scheduler::current_task();
    for (int t = 0; t < 30; ++t) {
        if (producer->state != TaskState::TERMINATED) {
            Scheduler::set_current(*producer);
            Scheduler::reschedule();
        }
        if (consumer->state != TaskState::TERMINATED) {
            Scheduler::set_current(*consumer);
            Scheduler::reschedule();
        }
        Scheduler::on_tick();
    }
    Scheduler::set_current(*original);

    JARVIS_ASSERT_EQ(42ULL, g_mp_observed);

    cleanup.dismiss();
    Scheduler::remove_task(*producer);
    producer->cleanup();
    delete producer;
    Scheduler::remove_task(*consumer);
    consumer->cleanup();
    delete consumer;

    JARVIS_TEST_PASS();
}

// ============================================================
// Test 4: Atomic increment race
// ============================================================
// Two tasks each increment a shared counter N times using
// atomic_fetch_add. Final counter must equal 2 * N.

static uint64_t* g_inc_counter;

static void inc_worker() {
    for (uint64_t i = 0; i < 500; ++i) {
        kernel::atomic_fetch_add(g_inc_counter, uint64_t(1));
    }
}

JARVIS_TEST(atomic_increment_race) {
    uint64_t counter_phys = PMM::alloc_page();
    JARVIS_ASSERT(counter_phys != 0);
    auto page_guard = ScopeGuard([&]() {
        PMM::free_page(counter_phys);
    });

    g_inc_counter = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + counter_phys);
    *g_inc_counter = 0;
    kernel::atomic_fence();

    auto* task_a = TaskControlBlock::create(inc_worker, 5, 10);
    JARVIS_ASSERT(task_a != nullptr);
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create(inc_worker, 5, 10);
    JARVIS_ASSERT(task_b != nullptr);
    Scheduler::add_task(*task_b);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*task_a);
        task_a->cleanup();
        delete task_a;
        Scheduler::remove_task(*task_b);
        task_b->cleanup();
        delete task_b;
    });

    auto* original = Scheduler::current_task();
    for (int t = 0; t < 20; ++t) {
        if (task_a->state != TaskState::TERMINATED) {
            Scheduler::set_current(*task_a);
            Scheduler::reschedule();
        }
        if (task_b->state != TaskState::TERMINATED) {
            Scheduler::set_current(*task_b);
            Scheduler::reschedule();
        }
        Scheduler::on_tick();
    }
    Scheduler::set_current(*original);

    JARVIS_ASSERT_EQ(1000ULL, *g_inc_counter);

    cleanup.dismiss();
    Scheduler::remove_task(*task_a);
    task_a->cleanup();
    delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup();
    delete task_b;

    JARVIS_TEST_PASS();
}

// ============================================================
// Test 5: Memory fence synchronization (ping-pong)
// ============================================================
// Two tasks ping-pong a turn variable using acquire/release semantics.

static uint64_t* g_pp_turn;
static uint64_t* g_pp_data1;
static uint64_t* g_pp_data2;
static int g_pp_rounds_a;
static int g_pp_rounds_b;

static void pp_worker_a() {
    for (int i = 0; i < 20; ++i) {
        while (kernel::atomic_load(g_pp_turn) != 0) {
            arch::pause();
        }
        kernel::atomic_acquire_fence();
        *g_pp_data1 = kernel::atomic_load(g_pp_data2) + 1;
        kernel::atomic_release_fence();
        kernel::atomic_store(g_pp_turn, uint64_t(1));
        ++g_pp_rounds_a;
    }
}

static void pp_worker_b() {
    for (int i = 0; i < 20; ++i) {
        while (kernel::atomic_load(g_pp_turn) != 1) {
            arch::pause();
        }
        kernel::atomic_acquire_fence();
        *g_pp_data2 = kernel::atomic_load(g_pp_data1) + 1;
        kernel::atomic_release_fence();
        kernel::atomic_store(g_pp_turn, uint64_t(0));
        ++g_pp_rounds_b;
    }
}

JARVIS_TEST(atomic_acquire_release_pingpong) {
    uint64_t turn_phys = PMM::alloc_page();
    uint64_t data1_phys = PMM::alloc_page();
    uint64_t data2_phys = PMM::alloc_page();
    JARVIS_ASSERT(turn_phys != 0 && data1_phys != 0 && data2_phys != 0);

    auto page_guard = ScopeGuard([&]() {
        PMM::free_page(turn_phys);
        PMM::free_page(data1_phys);
        PMM::free_page(data2_phys);
    });

    g_pp_turn = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + turn_phys);
    g_pp_data1 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + data1_phys);
    g_pp_data2 = reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + data2_phys);
    *g_pp_turn = 0;
    *g_pp_data1 = 0;
    *g_pp_data2 = 0;
    g_pp_rounds_a = 0;
    g_pp_rounds_b = 0;

    auto* task_a = TaskControlBlock::create(pp_worker_a, 5, 10);
    JARVIS_ASSERT(task_a != nullptr);
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create(pp_worker_b, 5, 10);
    JARVIS_ASSERT(task_b != nullptr);
    Scheduler::add_task(*task_b);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*task_a);
        task_a->cleanup();
        delete task_a;
        Scheduler::remove_task(*task_b);
        task_b->cleanup();
        delete task_b;
    });

    auto* original = Scheduler::current_task();
    for (int t = 0; t < 40; ++t) {
        if (task_a->state != TaskState::TERMINATED) {
            Scheduler::set_current(*task_a);
            Scheduler::reschedule();
        }
        if (task_b->state != TaskState::TERMINATED) {
            Scheduler::set_current(*task_b);
            Scheduler::reschedule();
        }
        Scheduler::on_tick();
    }
    Scheduler::set_current(*original);

    JARVIS_ASSERT_EQ(20, g_pp_rounds_a);
    JARVIS_ASSERT_EQ(20, g_pp_rounds_b);

    cleanup.dismiss();
    Scheduler::remove_task(*task_a);
    task_a->cleanup();
    delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup();
    delete task_b;

    JARVIS_TEST_PASS();
}

// ============================================================
// Registration
// ============================================================

void register_atomic_tests() {
    Logger::info("Registering atomic tests");
    JARVIS_REGISTER_TEST(atomic_load_store);
    JARVIS_REGISTER_TEST(atomic_exchange);
    JARVIS_REGISTER_TEST(atomic_compare_exchange);
    JARVIS_REGISTER_TEST(atomic_fetch_add);
    JARVIS_REGISTER_TEST(atomic_fetch_sub);
    JARVIS_REGISTER_TEST(atomic_fetch_and);
    JARVIS_REGISTER_TEST(atomic_fetch_or);
    JARVIS_REGISTER_TEST(atomic_64bit_operations);
    JARVIS_REGISTER_TEST(atomic_sb_litmus);
    JARVIS_REGISTER_TEST(atomic_mp_litmus);
    JARVIS_REGISTER_TEST(atomic_increment_race);
    JARVIS_REGISTER_TEST(atomic_acquire_release_pingpong);
}
