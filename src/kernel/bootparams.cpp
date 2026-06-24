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

#include <kernel/bootparams.hpp>
#include <kernel/multiboot2.hpp>
#include <string.hpp>

namespace kernel {

static BootParams g_boot_params = {
    .timer_hz = CONFIG_TICK_HZ,
    .max_tasks = CONFIG_MAX_TASKS,
    .scheduler_priority_ceiling = CONFIG_PRIORITY_CEILING,
    .preempt_enabled = CONFIG_PREEMPTION,
    .debug_scheduling = false,
    .debug_ipc = false,
    .debug_memory = false,
    .oom_killer_enabled = true,
};

BootParams& BootParams::instance() { return g_boot_params; }

static uint64_t parse_uint(const char*& s) {
    uint64_t value = 0;
    while (*s >= '0' && *s <= '9'
        ) { value = value * 10 + static_cast<uint64_t>(*s - '0'
        ); ++s; }
    return value;
}

void BootParams::parse_cstr(const char* cmdline) {
    if (!cmdline || !*cmdline) return;
    BootParams& bp = instance();

    while (*cmdline) {
        while (*cmdline == ' ') ++cmdline;
        if (!*cmdline) break;

        const char* key = cmdline;
        while (*cmdline && *cmdline != '=' && *cmdline != ' ') ++cmdline;
        size_t klen = static_cast<size_t>(cmdline - key);

        if (*cmdline == '=') {
            ++cmdline;
            const char* val_start = cmdline;
            while (*cmdline && *cmdline != ' ') ++cmdline;
            size_t vlen = static_cast<size_t>(cmdline - val_start);
            (void)vlen;

            if (klen == 8 && strncmp(key, "timer_hz", 8) == 0) {
                bp.timer_hz = parse_uint(val_start);
            } else if (klen == 9 && strncmp(key, "max_tasks", 9) == 0) {
                uint64_t value = parse_uint(val_start);
                if (value > 0 && value <= 128) bp.max_tasks = value;
            } else if (klen == 8 && strncmp(key, "priority", 8) == 0) {
                uint64_t value = parse_uint(val_start);
                if (value > 0) bp.scheduler_priority_ceiling = value;
            } else if (klen == 7 && strncmp(key, "preempt", 7) == 0) {
                bp.preempt_enabled = (*val_start == '1' || *val_start == 'y' ||
                    *val_start == 'Y');
            } else if (klen == 5 && strncmp(key, "debug", 5) == 0) {
                bool enabled = (*val_start == '1' || *val_start == 'y' ||
                    *val_start == 'Y');
                bp.debug_scheduling = enabled;
                bp.debug_ipc = enabled;
                bp.debug_memory = enabled;
            } else if (klen == 11 && strncmp(key, "debug_sched", 11) == 0) {
                bp.debug_scheduling = (*val_start == '1' || *val_start == 'y' ||
                    *val_start == 'Y');
            } else if (klen == 10 && strncmp(key, "oom_killer", 10) == 0) {
                bp.oom_killer_enabled = (*val_start == '1' ||
                    *val_start == 'y' ||
                                     *val_start == 'Y');
            }
        } else {
            if (klen == 4 && strncmp(key, "safe", 4) == 0) {
                bp.oom_killer_enabled = true;
            }
        }

        if (*cmdline) ++cmdline;
    }
}

void BootParams::parse_multiboot_cmdline() {
#if defined(CONFIG_ARCH_X86_64)
    uint64_t tag_addr = mb2_find_tag(1);
    if (!tag_addr) return;

    struct CmdlineTag {
        uint32_t type;
        uint32_t size;
        char     cmdline[];
    };
    auto* tag = reinterpret_cast<CmdlineTag*>(tag_addr);
    parse_cstr(tag->cmdline);
#endif
}

}
