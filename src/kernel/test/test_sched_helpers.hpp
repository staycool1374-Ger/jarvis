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

/// @file test_sched_helpers.hpp
/// @brief Safe test helpers for scheduler operations (set_current, reschedule).
///
/// Tests run under the INIT task (IF=1).  The timer ISR fires
/// rate_monotonic_schedule(), which can consume stale context-switch globals
/// or undo set_current() via scheduler_on_context_switch().  These helpers
/// disable interrupts to prevent ISR interference.
///
/// Use yield_as() for "set_current + reschedule" pairs.
/// Use ScopedCurrentTask for scoped current-task changes (NO blocking ops).
/// Use raw Scheduler::set_current() for blocking operations or deliberate
/// preemption-simulation tests.

#pragma once

#include <kernel/arch/irq_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

namespace kernel::test {

/// @brief Atomically set a task as current and call reschedule, with
///        interrupts disabled to prevent timer ISR interference between
///        the two operations.  This restores the IF=0 semantics that
///        tests using the "set_current + reschedule" pattern were
///        originally designed for.
inline void yield_as(TaskControlBlock &task) noexcept {
    arch::IrqGuard guard;
    Scheduler::set_current(task);
    Scheduler::reschedule();
}

/// @brief RAII guard that sets a task as current for the duration of
///        a scope, restoring the original task on destruction.
///        Interrupts are disabled for the entire scope to prevent the
///        timer ISR from consuming stale globals or undoing set_current().
///
///        NOTE: Do NOT perform blocking operations (semaphore wait, mutex
///        lock with contention, IPC receive) inside this scope — the
///        scheduler cannot context-switch with IF=0.  For blocking
///        patterns, use raw Scheduler::set_current() with the defensive
///        cleanup in ~ScopedCurrentTask.
class [[nodiscard]] ScopedCurrentTask {
    TaskControlBlock *saved_;
    arch::IrqGuard guard_;

  public:
    explicit ScopedCurrentTask(TaskControlBlock &task) noexcept
        : saved_(Scheduler::current_task()) {
        Scheduler::set_current(task);
    }

    ~ScopedCurrentTask() noexcept {
        if (saved_) {
            Scheduler::set_current(*saved_);
        }
    }

    ScopedCurrentTask(const ScopedCurrentTask &) = delete;
    ScopedCurrentTask &operator=(const ScopedCurrentTask &) = delete;
};

} // namespace kernel::test
