#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>

namespace kernel {

constexpr uint64_t SYSCALL_MAX_PATH = 256;

/// @brief Get the currently running task from the scheduler.
/// @return Pointer to the current TaskControlBlock.
TaskControlBlock* syscall_task();
/// @brief Check if the current task is a user-space task.
/// @return true if user task (has user page table).
bool syscall_is_user_task();
/// @brief Open a vnode as a file descriptor in the current task's fd table.
/// @return The fd index, or VFS_INVALID on failure.
int syscall_task_open(vfs::Vnode* vn, uint64_t flags);
/// @brief Resolve a path and open it as a file descriptor.
/// @return The fd index, or VFS_INVALID on failure.
int syscall_path_open(const char* path, uint64_t flags);

} // namespace kernel
