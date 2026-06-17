#pragma once

#include <types.hpp>

namespace kernel {

struct TaskControlBlock;

namespace vfsd {

static constexpr uint64_t VFS_OPEN     = 100;
static constexpr uint64_t VFS_CLOSE    = 101;
static constexpr uint64_t VFS_READ     = 102;
static constexpr uint64_t VFS_WRITE    = 103;
static constexpr uint64_t VFS_STAT     = 104;
static constexpr uint64_t VFS_FSTAT    = 105;
static constexpr uint64_t VFS_MOUNT    = 106;
static constexpr uint64_t VFS_CHDIR    = 107;
static constexpr uint64_t VFS_GETCWD   = 108;
static constexpr uint64_t VFS_READDIR  = 109;
static constexpr uint64_t VFS_MKDIR    = 110;
static constexpr uint64_t VFS_UNLINK   = 111;
static constexpr uint64_t VFS_RMDIR    = 112;

struct Msg {
    uint64_t sender_id;
    uint64_t type;
    uint64_t arg0;
    uint64_t arg1;
    char     path[32];
};

struct Reply {
    int64_t result;
    uint64_t data0;
    uint64_t data1;
    uint64_t data2;
    uint64_t data3;
};

/// @brief Record the PID of the VFS daemon task.
void set_vfsd_pid(uint64_t pid);
/// @brief Get the recorded VFS daemon PID.
/// @return The PID, or 0 if not yet set.
uint64_t get_vfsd_pid();
/// @brief Check if the current task is the VFS daemon.
/// @return true if the current task's PID matches the daemon PID.
bool is_vfsd_task();

} // namespace vfsd
} // namespace kernel