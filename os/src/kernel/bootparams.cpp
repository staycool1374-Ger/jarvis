#include <kernel/bootparams.hpp>
#include <kernel/multiboot2.hpp>
#include <string.hpp>

namespace kernel {

static BootParams g_boot_params = {
    .timer_hz = 1000,
    .max_tasks = 64,
    .scheduler_priority_ceiling = 255,
    .preempt_enabled = true,
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
    uint64_t tag_addr = mb2_find_tag(1);
    if (!tag_addr) return;

    struct CmdlineTag {
        uint32_t type;
        uint32_t size;
        char     cmdline[];
    };
    auto* tag = reinterpret_cast<CmdlineTag*>(tag_addr);
    parse_cstr(tag->cmdline);
}

}
