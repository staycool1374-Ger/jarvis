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

/// @name Test-isolation helpers
/// @brief Copy all daemon entries and count into @p entries_out / @p num_out.
void capture_state(DaemonEntry* entries_out, uint64_t& num_out);
/// @brief Restore daemon entries and count from @p entries_in / @p num_in.
void restore_state(const DaemonEntry* entries_in, uint64_t num_in);

} // namespace daemon
} // namespace kernel
