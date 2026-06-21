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

#pragma once

#include <types.hpp>

namespace kernel {
namespace daemon {

static constexpr uint64_t MAX_DAEMONS = 8;
static constexpr uint64_t MAX_RESTART_COUNT = 10;

struct DaemonEntry {
    const char* name;
    const char* initrd_path;
    uint64_t pid;
    uint64_t restart_count;
    void (*set_pid_fn)(uint64_t);
    uint64_t (*get_pid_fn)();
};

/// @brief Initialize the daemon manager and spawn registered daemons.
void init();
/// @brief Kill and re-register all daemons (used during recovery).
void reset_daemons();
/// @brief Register a daemon for lifecycle management.
/// @param name Human-readable name for the daemon.
/// @param initrd_path Path to the daemon ELF in the initrd.
/// @param set_pid Callback to store the PID when the daemon is spawned.
/// @param get_pid Callback to retrieve the daemon's current PID.
/// @return true if registered successfully.
bool register_daemon(const char* name, const char* initrd_path,
                     void (*set_pid)(uint64_t), uint64_t (*get_pid)());
/// @brief Notify the daemon manager that a daemon has exited.
void notify_death(uint64_t pid);
/// @brief Restart any daemons that have died (up to MAX_RESTART_COUNT each).
void restart_stale_daemons();

/// @brief Reset a daemon's restart count so it can be restarted again.
///        Sets restart_count=0, pid=0, and calls set_pid_fn(0).
void reset_restart_count(const char* name);

/// @brief Return a const reference to the i-th daemon entry.
const DaemonEntry& get_entry(uint64_t index);

/// @name Test-isolation helpers
/// @brief Copy all daemon entries and count into @p entries_out / @p num_out.
void capture_state(DaemonEntry* out_entries, uint64_t& num_out);
/// @brief Restore daemon entries and count from @p entries_in / @p num_in.
void restore_state(const DaemonEntry* entries_in, uint64_t num_in);

} // namespace daemon
} // namespace kernel
