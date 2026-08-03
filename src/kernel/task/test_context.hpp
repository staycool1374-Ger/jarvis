#pragma once

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

/// @file test_context.hpp
/// @brief Test-runner context injected into the scheduler (PfA-A).

#include <types.hpp>

namespace kernel {

/// @brief Test-runner state that the scheduler's ISR-visible paths must read.
///
/// PfA-A: production boots with `nullptr` here, so every test flag resolves
/// to its compile-time-false default and none of the state below exists as a
/// scheduler global.  The harness constructs one instance, calls
/// Scheduler::set_test_context(), and clears it back to nullptr afterwards.
struct TestContext {
    bool test_active = false;             ///< In-shell test cycle running.
    uint64_t deadline_monitor_pid = 0;    ///< Live [deadline-mon] task for the
                                          ///< CONFIG_DEADLINE_ACTION==4 path.
    uint64_t dummy_save_rsp = 0;          ///< Test-injected deferred-switch RSP.
};

} // namespace kernel
