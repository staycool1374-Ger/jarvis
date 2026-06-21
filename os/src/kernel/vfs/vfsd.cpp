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

#include <kernel/vfs/vfsd.hpp>
#include <kernel/task/scheduler.hpp>

namespace kernel {
namespace vfsd {

static uint64_t g_vfsd_pid = 0;

void set_vfsd_pid(uint64_t pid) {
    g_vfsd_pid = pid;
}

uint64_t get_vfsd_pid() {
    return g_vfsd_pid;
}

bool is_vfsd_task() {
    auto* cur = Scheduler::current_task();
    return cur && cur->id == g_vfsd_pid;
}

} // namespace vfsd
} // namespace kernel