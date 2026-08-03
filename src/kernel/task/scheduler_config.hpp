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

/// @file scheduler_config.hpp
/// @brief Scheduler boot-time configuration (PfA-A — parameterised from above).

#include <types.hpp>

namespace kernel {

/// @brief Boot-time scheduler configuration, injected from kernel_init.
///
/// PfA-A (PARAMETERISE FROM ABOVE): these values flow DOWN from the top-level
/// initialiser into Scheduler::init(cfg) instead of being hardcoded defaults
/// reachable as globals.  The scheduler copies them into private statics at
/// init (zero-per-call overhead); nothing else reads the config global.
struct SchedulerConfig {
    bool preempt_enabled = true;          ///< Preemption allowed after init.
    uint64_t sporadic_task_count = 0;     ///< Initial sporadic-server task count.
    bool suppress_terminated_log = false; ///< Silence "task terminated" logs.
};

} // namespace kernel
