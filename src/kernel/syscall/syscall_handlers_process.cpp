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

/// @file syscall_handlers_process.cpp
/// @brief Syscall handlers for process operations: exec, fork, waitpid, getpid, kill, etc.

#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/test/test_isolate.hpp>
#include <signal.hpp>
#include <constants.hpp>
#include <string.hpp>

namespace kernel {

uint64_t Syscall::sys_fork(uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t* regs) {
    if (!regs) return static_cast<uint64_t>(-1);
    auto* child = TaskControlBlock::clone(regs);
    if (!child) return static_cast<uint64_t>(-1);
    Scheduler::add_task(*child);
    return child->id;
}

uint64_t Syscall::sys_waitpid(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    uint64_t target_pid = arg0;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto status = checked(reinterpret_cast<uint64_t*>(arg1));
    auto* status_ptr = (!syscall_is_user_task() || status.valid()) ?
        status.unsafe_ptr() : nullptr;
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    TaskControlBlock* child = nullptr;
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->parent_id == cur->id) {
            if (target_pid == static_cast<uint64_t>(-1) || t->id == target_pid
                ) {
                child = t;
                break;
            }
        }
    }
    if (!child) return static_cast<uint64_t>(-1);
        if (child->state == TaskState::TERMINATED) {
        if (status_ptr) *status_ptr = child->exit_code;
        uint64_t cid = child->id;
        cur->remove_child(child);
        child->cleanup();
        Scheduler::remove_task(*child);
        MemPool::free(child);
        return cid;
    }
    if (arg2 & 1) return 0;
    cur->waiting_child_pid = target_pid;
    cur->waiting_child_status = status_ptr;
    cur->state = TaskState::BLOCKED;
    return static_cast<uint64_t>(-1);
}

static bool validate_argv_envp(const char* const* ptr, bool is_user_task) {
    if (!ptr) return true;
    if (is_user_task) {
        auto arr = checked(ptr, static_cast<size_t>(1));
        if (!arr.valid()) return false;
        const char* const* p = ptr;
        while (*p) {
            auto s = checked(*p, static_cast<size_t>(1));
            if (!s.valid()) return false;
            size_t len = 0;
            while (s.unsafe_ptr()[len]) ++len;
            ++p;
            auto next = checked(p, static_cast<size_t>(1));
            if (!next.valid()) return false;
        }
    }
    return true;
}

uint64_t Syscall::sys_exec(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t* regs) {
    kernel::test::mark_vfs_touched();
    if (!syscall_is_user_task()) return static_cast<uint64_t>(-1);
    vfs::Vnode* vn = nullptr;
    char path_buf[SYSCALL_MAX_PATH];
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0),
        SYSCALL_MAX_PATH))
        return static_cast<uint64_t>(-1);
    vn = vfs::resolve(path_buf);
    if (!vn) return static_cast<uint64_t>(-1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* argv = reinterpret_cast<const char* const*>(arg1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* envp = reinterpret_cast<const char* const*>(arg2);
    if (!validate_argv_envp(argv, true)) return static_cast<uint64_t>(-1);
    if (!validate_argv_envp(envp, true)) return static_cast<uint64_t>(-1);
    if (vn->size == 0 || vn->size > 512_KiB) return static_cast<uint64_t>(-1);
    size_t file_pages = (static_cast<size_t>(vn->size) + 4095) / 4096;
    uint64_t file_phys = PMM::alloc_contiguous(file_pages);
    if (!file_phys) return static_cast<uint64_t>(-1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    uint8_t* file_buf = reinterpret_cast<uint8_t*>(arch::HHDM_OFFSET + file_phys
        );
    int64_t r = vn->ops->read(*vn, file_buf, vn->size, 0);
    if (r <= 0 || static_cast<uint64_t>(r) != vn->size) {
        for (size_t i = 0; i < file_pages; ++i)
            PMM::free_page(file_phys + i * 4096);
        return static_cast<uint64_t>(-1);
    }
    auto* hdr = reinterpret_cast<const elf::ELF64Header*>(file_buf);
    if (!elf::exec_into_current(hdr, file_buf, argv, envp, regs)) {
        for (size_t i = 0; i < file_pages; ++i)
            PMM::free_page(file_phys + i * 4096);
        return static_cast<uint64_t>(-1);
    }
    for (size_t i = 0; i < file_pages; ++i)
        PMM::free_page(file_phys + i * 4096);
    return 0;
}

uint64_t Syscall::sys_kill(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    uint64_t target_pid = arg0;
    uint64_t sig = arg1;
    if (sig >= MAX_SIGNAL_HANDLERS) return static_cast<uint64_t>(-1);
    // SIG_NONE (signal 0) is the null signal — POSIX existence check, no delivery
    if (sig == static_cast<uint64_t>(kernel::Signal::SIG_NONE)) return 0;
    auto* t = Scheduler::find_task(target_pid);
    if (!t) return static_cast<uint64_t>(-1);
    if (t == syscall_task()) {
        if (signal_is_fatal(sig) || !t->has_signal_handler(sig)) {
            t->state = TaskState::TERMINATED;
            t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
        } else {
            t->pending_signals |= (1ULL << sig);
        }
    } else {
        t->pending_signals |= (1ULL << sig);
    }
    return 0;
}

uint64_t Syscall::sys_signal(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    auto* t = syscall_task();
    if (!t) return static_cast<uint64_t>(-1);
    uint64_t sig = arg0;
    if (sig >= MAX_SIGNAL_HANDLERS) return static_cast<uint64_t>(-1);
    if (signal_is_fatal(sig)) return static_cast<uint64_t>(-1);
    // SIG_NONE cannot be caught or ignored
    if (sig == static_cast<uint64_t>(kernel::Signal::SIG_NONE)) return 0;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto handler = reinterpret_cast<sighandler_t>(arg1);
    t->set_signal_handler(sig, handler);
    return 0;
}

uint64_t Syscall::sys_sigreturn(uint64_t, uint64_t, uint64_t, uint64_t,
    uint64_t* regs) {
    auto* t = syscall_task();
    if (!t || !regs) return static_cast<uint64_t>(-1);
    uint64_t user_rsp = regs[20];
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* frame = reinterpret_cast<const SignalFrame*>(
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        reinterpret_cast<uint64_t*>(user_rsp));
    auto chk = checked(frame, 1);
    if (!chk.valid()) return static_cast<uint64_t>(-1);
    regs[17] = frame->saved_rip;
    regs[20] = frame->saved_rsp;
    regs[19] = frame->saved_rflags;
    regs[18] = frame->saved_cs;
    regs[21] = frame->saved_ss;
    regs[0] = 0;
    return 0;
}

} // namespace kernel
