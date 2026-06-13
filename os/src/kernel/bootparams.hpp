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
