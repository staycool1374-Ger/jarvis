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

/// @file syscall.cpp
/// @brief System call dispatcher — init (MSR setup), handle (table dispatch), helpers.

#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/arch/msr.hpp>
#include <constants.hpp>

extern "C" void syscall_entry();

namespace kernel {

/// @brief Initialise the syscall interface (MSR setup, syscall-table built at compile time).
void Syscall::init() {
#if defined(CONFIG_ARCH_X86_64)
    uint64_t star_val = (static_cast<uint64_t>(arch::SEG_KERNEL_CODE) << 32) |
                        (static_cast<uint64_t>(arch::SEG_USER_CODE) << 48);
    arch::wrmsr(arch::IA32_STAR, star_val);
    arch::wrmsr(arch::IA32_LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    arch::wrmsr(arch::IA32_FMASK, 0x200);
#elif defined(CONFIG_ARCH_AARCH64)
    // AArch64 syscalls use SVC #0; VBAR_EL1 entry point handles dispatch.
    // No MSR-based syscall setup needed.
    (void)syscall_entry;
#endif
}

/// @brief Get the currently running task from the scheduler.
TaskControlBlock* syscall_task() {
    return Scheduler::current_task();
}

/// @brief Check if the current task is a user-space task (has its own page table).
bool syscall_is_user_task() {
    auto* t = syscall_task();
    return t && t->page_table_ != 0;
}

/// @brief Open a vnode as a file descriptor in the current task's fd table.
int syscall_task_open(vfs::Vnode* vn, uint64_t flags) {
    int fd = syscall_task()->fd_table.alloc();
    if (fd < 0) return -1;
    syscall_task()->fd_table.fds[fd].vnode = vn;
    syscall_task()->fd_table.fds[fd].offset = 0;
    syscall_task()->fd_table.fds[fd].flags = flags;
    if (vn->ops->open) vn->ops->open(*vn, flags);
    return fd;
}

/// @brief Resolve a path and open it as a file descriptor.
int syscall_path_open(const char* path, uint64_t flags) {
    vfs::Vnode* vn = vfs::resolve(path);
    if (!vn) {
        if (flags & vfs::O_CREAT) {
            if (vfs::create(path, vfs::S_IFREG) != 0) return -1;
            vn = vfs::resolve(path);
        }
        if (!vn) return -1;
    }
    return syscall_task_open(vn, flags);
}

uint64_t Syscall::handle(uint64_t number, uint64_t arg0, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3, uint64_t* regs)
{
    if (number >= static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL))
        return static_cast<uint64_t>(-1);
    return syscall_table_[number](arg0, arg1, arg2, arg3, regs);
}

} // namespace kernel
