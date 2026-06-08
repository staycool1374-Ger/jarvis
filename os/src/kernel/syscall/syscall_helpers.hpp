#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>

namespace kernel {

constexpr uint64_t SYSCALL_MAX_PATH = 256;

TaskControlBlock* syscall_task();
bool syscall_is_user_task();
int syscall_task_open(vfs::Vnode* vn, uint64_t flags);
int syscall_path_open(const char* path, uint64_t flags);

} // namespace kernel
