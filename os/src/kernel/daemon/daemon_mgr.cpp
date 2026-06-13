#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/elf/elf.hpp>
#include <initrd/initrd.hpp>
#include <logger.hpp>

extern "C" void debug_write(const char* s);
extern "C" void debug_write_hex(uint64_t v);

namespace kernel {
namespace daemon {

static DaemonEntry entries_[MAX_DAEMONS] = {};
static uint64_t num_daemons_ = 0;

void init() {
    for (uint64_t i = 0; i < MAX_DAEMONS; ++i) {
        entries_[i].name = nullptr;
        entries_[i].pid = 0;
    }
    num_daemons_ = 0;
}

bool register_daemon(const char* name, const char* initrd_path,
                     void (*set_pid)(uint64_t), uint64_t (*get_pid)()) {
    if (num_daemons_ >= MAX_DAEMONS) return false;

    entries_[num_daemons_].name = name;
    entries_[num_daemons_].initrd_path = initrd_path;
    entries_[num_daemons_].pid = get_pid ? get_pid() : 0;
    entries_[num_daemons_].restart_count = 0;
    entries_[num_daemons_].set_pid_fn = set_pid;
    entries_[num_daemons_].get_pid_fn = get_pid;
    ++num_daemons_;

    Logger::info("daemon_mgr: registered '%s' at path '%s' (PID=%d)",
                 name, initrd_path,
                 entries_[num_daemons_ - 1].pid);
    return true;
}

void reset_daemons() {
    for (uint64_t i = 0; i < num_daemons_; ++i) {
        entries_[i].restart_count = 0;
        entries_[i].pid = 0;
        if (entries_[i].set_pid_fn) {
            entries_[i].set_pid_fn(0);
        }
    }
    Logger::info("daemon_mgr: all daemon states reset");
}

void notify_death(uint64_t pid) {
    for (uint64_t i = 0; i < num_daemons_; ++i) {
        if (entries_[i].pid == pid) {
            debug_write("[DAEMON] '");
            debug_write(entries_[i].name);
            debug_write("' died (PID=");
            debug_write_hex(pid);
            debug_write("), restart_count=");
            debug_write_hex(entries_[i].restart_count);
            debug_write("\n");

            // Reset PID via the module's setter so all IPC callers immediately fail
            if (entries_[i].set_pid_fn) {
                entries_[i].set_pid_fn(0);
            }
            entries_[i].pid = 0;
            return;
        }
    }
}

void restart_stale_daemons() {
    for (uint64_t i = 0; i < num_daemons_; ++i) {
        if (entries_[i].pid != 0) continue;

        // Check restart limit
        if (entries_[i].restart_count >= MAX_RESTART_COUNT) {
            Logger::warn("daemon_mgr: '%s' restart limit reached (%d), giving up",
                         entries_[i].name,
                         entries_[i].restart_count);
            continue;
        }

        // Try to find the initrd file
        initrd::InitrdFile f = initrd::find(entries_[i].initrd_path);
        if (!f.data) {
            // Try with ./ prefix
            char with_dot[256];
            with_dot[0] = '.';
            with_dot[1] = '/';
            size_t j = 0;
            while (entries_[i].initrd_path[j] && j < 254) {
                with_dot[j + 2] = entries_[i].initrd_path[j];
                ++j;
            }
            with_dot[j + 2] = '\0';
            f = initrd::find(with_dot);
        }
        if (!f.data) {
            Logger::warn("daemon_mgr: '%s' initrd file not found, cannot restart",
                         entries_[i].name);
            continue;
        }

        auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
        if (!kernel::elf::validate_header(hdr)) {
            Logger::warn("daemon_mgr: '%s' invalid ELF header, cannot restart",
                         entries_[i].name);
            continue;
        }

        auto* task = kernel::elf::load(hdr, f.data);
        if (!task) {
            Logger::warn("daemon_mgr: '%s' elf::load failed, cannot restart",
                         entries_[i].name);
            continue;
        }

        task->priority = 1;
        task->period_ticks = 10;
        kernel::Scheduler::add_task(*task);

        ++entries_[i].restart_count;
        entries_[i].pid = task->id;
        if (entries_[i].set_pid_fn) {
            entries_[i].set_pid_fn(task->id);
        }

        debug_write("[DAEMON] '");
        debug_write(entries_[i].name);
        debug_write("' restarted (PID=");
        debug_write_hex(task->id);
        debug_write(", restart #");
        debug_write_hex(entries_[i].restart_count);
        debug_write(")\n");
    }
}

} // namespace daemon
} // namespace kernel
