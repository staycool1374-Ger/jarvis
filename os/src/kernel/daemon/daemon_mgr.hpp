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

void init();
void reset_daemons();
bool register_daemon(const char* name, const char* initrd_path,
                     void (*set_pid)(uint64_t), uint64_t (*get_pid)());
void notify_death(uint64_t pid);
void restart_stale_daemons();

} // namespace daemon
} // namespace kernel
