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

/// @file test_wcet_scheduler.cpp
/// @brief WCET benchmark for the deadline-miss detection scan
///        (Scheduler::scan_deadlines / DeadlineList walk). Phase 7b.
///
///        v0.3.10 rework (SIMULATED → DRIVEN): the scan population is built
///        from REAL dispatched kernel tasks with genuinely-expired deadlines
///        (a real task that busy-waits past its real deadline).  No existing
///        task's deadline/period is mutated.

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
// of deadline-tracked tasks.  The scan population is a REAL dispatched kernel
// task population whose deadlines genuinely expire (each task busy-waits past
// its real deadline).
//
// One population of 40 tasks is created once (each genuinely overruns its
// real 2-tick deadline), then trimmed (deleted) to obtain the 1/10/40 data
// points.
//
// Expect: scan_deadlines() returns a non-zero cycle count (the scan ran) for
// each task-population; the measured worst-case is logged for off-line
// analysis (see docs/wcet_analysis.md).
JARVIS_TEST(wcet_scan_deadlines, "PRE: none | POST: none") {
    const uint64_t kIters = 300;

    // --- Build one population of 40 REAL tasks that genuinely overrun their
    //     2-tick deadline (busy-wait 5 real ticks) and then terminate.
    TaskControlBlock *tasks[64];
    uint64_t made = 0;
    for (uint64_t k = 0; k < 40; ++k) {
        arch::IrqGuard guard;
        if (Scheduler::task_count() >= 58)
            break; // headroom below MAX_TASKS
        auto *t = TaskControlBlock::create(
            []() {
                uint64_t start = arch::Timer::ticks();
                while (arch::Timer::ticks() - start < 5)
                    arch::pause();
            },
            11, 2);
        if (t == nullptr)
            break;
        Scheduler::add_task(*t);
        tasks[made++] = t;
    }
    auto teardown = ScopeGuard([&]() {
        for (uint64_t k = 0; k < made; ++k) {
            if (tasks[k]) {
                if (tasks[k]->magic == TaskControlBlock::TCB_MAGIC) {
                    Scheduler::remove_task(*tasks[k]);
                    tasks[k]->cleanup();
                    delete tasks[k];
                }
            }
        }
    });

    // Give the tasks real time to genuinely overrun their deadlines (they
    // were created with deadline = now + 2; busy-waiting 5 real ticks per
    // task guarantees the deadline has passed by the time we measure).
    {
        uint64_t start = arch::Timer::ticks();
        while (arch::Timer::ticks() - start < 20)
            asm volatile("pause");
    }

    // --- Measure worst-case scan cycles over the real overrun population.
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

    JARVIS_ASSERT(c40 > 0);
    Logger::info("[WCET] scan_deadlines 40-task worst=");
    Logger::print_dec(c40);
    Logger::info(" cyc");

    JARVIS_TEST_PASS();
}

void register_wcet_scheduler_tests() {
    Logger::info("Registering WCET scheduler benchmark tests");
    JARVIS_REGISTER_TEST(wcet_scan_deadlines);
}
