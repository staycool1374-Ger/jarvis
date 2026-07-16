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

/// @file test_wcet_scheduler.cpp
/// @brief WCET benchmark for the deadline-miss detection scan
///        (Scheduler::scan_deadlines / DeadlineList walk). Phase 7b.

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/hal/irq_guard.hpp>
#include <kernel/test/test_sched_helpers.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Measure the worst-case execution time (WCET) of the deadline
// miss-detection scan (Scheduler::scan_deadlines) as a function of the number
// of deadline-tracked tasks. This exercises the O(n) walk that backs both the
// monitor-task path (CONFIG_DEADLINE_MONITOR_TASK=1) and the inline on_tick
// path.
//
// To obtain a *clean* scan cost (without the variance of serial-logging inside
// the miss handler), every pre-existing task's deadline is pushed far into the
// future and its period cleared before the measurement; the benchmark tasks
// themselves carry far-future deadlines so no miss fires. IRQs are disabled
// around each measured scan so the result reflects the synchronous scan cost.
//
// One population of 40 tasks is created once, then trimmed (deleted) to obtain
// the 1/10/40 data points without repeated create/delete churn.
//
// Expect: scan_deadlines() returns a non-zero cycle count (the scan ran) for
// each task-population; the measured worst-case is logged for off-line
// analysis (see docs/wcet_analysis.md).
JARVIS_TEST(wcet_scan_deadlines, "PRE: none | POST: none") {
    const uint64_t kIters = 300;

    // --- Neutralize pre-existing deadlines so the scan does pure iteration.
    uint64_t const saved_count = Scheduler::task_count();
    uint64_t saved_dl[64];
    uint64_t saved_period[64];
    {
        arch::IrqGuard guard;
        uint64_t const far = arch::Timer::ticks() + 1'000'000'000ULL;
        for (uint64_t i = 0; i < saved_count && i < 64; ++i) {
            auto *t = Scheduler::task_at(i);
            if (t) {
                saved_dl[i] = t->deadline_ticks;
                saved_period[i] = t->period_ticks;
                t->deadline_ticks = far + i;
                t->period_ticks = 0;
            }
        }
    }
    auto restore_deadlines = ScopeGuard([&]() {
        arch::IrqGuard guard;
        for (uint64_t i = 0; i < saved_count && i < 64; ++i) {
            auto *t = Scheduler::task_at(i);
            if (t) {
                t->deadline_ticks = saved_dl[i];
                t->period_ticks = saved_period[i];
            }
        }
    });

    // --- Build one population of 40 far-future-deadline tasks living in the
    //     scheduler's own DeadlineList (period>0 && deadline>0).
    TaskControlBlock *tasks[64];
    uint64_t made = 0;
    {
        uint64_t const base = arch::Timer::ticks();
        for (uint64_t k = 0; k < 40; ++k) {
            arch::IrqGuard guard;
            if (Scheduler::task_count() >= 58)
                break; // headroom below MAX_TASKS
            auto *t = TaskControlBlock::create([]() {}, 10, 10);
            if (t == nullptr)
                break;
            t->base_priority = 10;
            t->priority = 10;
            t->period_ticks = 10;
            t->deadline_ticks = base + 2'000'000'000ULL + k;
            Scheduler::add_task(*t);
            Scheduler::dequeue_ready(*t);
            {
                kernel::test::ScopedCurrentTask scope(*t);
                t->state = TaskState::BLOCKED;
            }
            tasks[made++] = t;
        }
    }
    auto teardown = ScopeGuard([&]() {
        for (uint64_t k = 0; k < made; ++k) {
            if (tasks[k]) {
                tasks[k]->cleanup();
                delete tasks[k];
            }
        }
    });

    // --- Measure worst-case scan cycles.
    auto measure = [&]() -> uint64_t {
        uint64_t max_cycles = 0;
        for (uint64_t it = 0; it < kIters; ++it) {
            arch::IrqGuard guard;
            uint64_t const s = arch::rdtsc();
            Scheduler::scan_deadlines();
            uint64_t const e = arch::rdtsc();
            uint64_t const d = (e > s) ? (e - s) : 0;
            if (d > max_cycles)
                max_cycles = d;
        }
        return max_cycles;
    };

    uint64_t const c40 = measure();
    // Trim to 10: delete the last 30.
    for (uint64_t k = 30; k < made; ++k) {
        if (tasks[k]) {
            tasks[k]->cleanup();
            delete tasks[k];
            tasks[k] = nullptr;
        }
    }
    uint64_t const c10 = measure();
    // Trim to 1: delete tasks 1..9.
    for (uint64_t k = 1; k < made && k < 10; ++k) {
        if (tasks[k]) {
            tasks[k]->cleanup();
            delete tasks[k];
            tasks[k] = nullptr;
        }
    }
    uint64_t const c1 = measure();

    Logger::info("[WCET] scan_deadlines 1-task  worst=");
    Logger::print_dec(c1);
    Logger::info(" cyc");
    Logger::info("[WCET] scan_deadlines 10-task worst=");
    Logger::print_dec(c10);
    Logger::info(" cyc");
    Logger::info("[WCET] scan_deadlines 40-task worst=");
    Logger::print_dec(c40);
    Logger::info(" cyc");

    JARVIS_ASSERT(c1 > 0);
    JARVIS_ASSERT(c10 > 0);
    JARVIS_ASSERT(c40 > 0);
    JARVIS_TEST_PASS();
}

void register_wcet_scheduler_tests() {
    Logger::info("Registering WCET scheduler benchmark tests");
    JARVIS_REGISTER_TEST(wcet_scan_deadlines);
}
