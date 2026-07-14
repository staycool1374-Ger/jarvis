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

/// @file iocd.cpp
/// @brief I/O Controller Daemon IPC implementation.

#include <kernel/driver/iocd.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace iocd {

static uint64_t g_iocd_pid = 0;

/// @brief Record the PID of the I/O Controller Daemon.
/// @param pid The IOCD task PID.
void set_iocd_pid(uint64_t pid) {
    g_iocd_pid = pid;
}

/// @brief Retrieve the recorded IOCD PID.
/// @return The PID, or 0 if not yet registered.
uint64_t get_iocd_pid() {
    return g_iocd_pid;
}

/// @brief Check whether the currently running task is the IOCD.
/// @return true if the current task PID matches the recorded IOCD PID.
bool is_iocd_task() {
    auto *cur = Scheduler::current_task();
    return cur && cur->id == g_iocd_pid;
}

} // namespace iocd
} // namespace kernel
