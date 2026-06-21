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

struct BootParams {
    uint64_t timer_hz;
    uint64_t max_tasks;
    uint64_t scheduler_priority_ceiling;
    bool     preempt_enabled;
    bool     debug_scheduling;
    bool     debug_ipc;
    bool     debug_memory;
    bool     oom_killer_enabled;

    /// @brief Get the singleton BootParams instance.
    static BootParams& instance();

    /// @brief Parse the multiboot command line string into BootParams fields.
    static void parse_multiboot_cmdline();

    /// @brief Parse a raw command-line string, setting BootParams fields.
    /// @param cmdline Null-terminated command-line string.
    static void parse_cstr(const char* cmdline);
};

}
