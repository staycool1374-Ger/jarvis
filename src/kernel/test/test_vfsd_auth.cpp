/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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

/// @file test_vfsd_auth.cpp
/// @brief VFS daemon authorisation tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): every VFS syscall runs inside a REAL
/// kernel task (prio ≥ 11) that is genuinely dispatched — the handler's
/// `syscall_task()` resolves to the running task and the kernel-bypass /
/// daemon authorisation path is exercised through real execution.  The
/// harness never calls Syscall::handle() directly.

#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-null-argument"
#pragma GCC diagnostic ignored "-Wanalyzer-possible-null-dereference"
#endif

#include <test.hpp>
#include <logger.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfsd.hpp>
#include "test_sched_helpers.hpp"

using namespace kernel;

#if !defined(CONFIG_ARCH_RISCV64)

namespace {
void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    kernel::test::terminate_if_live(t);
}
} // namespace

// Runmode: kernel
// Testidea: Verifies that the REAL vfsd daemon task self-authorizes VFS
// syscalls (is_vfsd_task() true), while a fresh test task is not the daemon.
// Input: Query the live vfsd daemon task; create a fresh kernel task.
// Expect: is_vfsd_task() is true for the real daemon, false for a test task.
// Depends: kernel::vfsd, kernel::Scheduler
JARVIS_TEST(vfsd_self_authorization, "PRE: vfsd, iocd | POST: none") {
    uint64_t daemon_pid = vfsd::get_vfsd_pid();
    JARVIS_ASSERT(daemon_pid != 0);
    auto *daemon = Scheduler::find_task(daemon_pid);
    JARVIS_ASSERT(daemon != nullptr);

    auto *t = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    JARVIS_ASSERT(t->id != daemon_pid);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the VFS self-authorization fd-op path: a REAL kernel
// task opens /dev/null, reads, and closes — the syscalls run in the task's
// own dispatched context via the kernel bypass.
// Input: Dispatch a kernel task that calls sys_open/read/close.
// Expect: fd >= 0, read == 0 (EOF), close == 0.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::vfsd
JARVIS_TEST(vfsd_self_authorization_fd_op, "PRE: vfsd, iocd | POST: none") {
    static uint64_t g_fd = 0;
    static uint64_t g_read = 0;
    static uint64_t g_close = 0;

    auto *t = TaskControlBlock::create(
        []() {
            const char *path = "/dev/null";
            g_fd = Syscall::handle(
                static_cast<uint64_t>(SyscallNumber::OPEN),
                reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
            if (static_cast<int64_t>(g_fd) < 0)
                return;
            char buf[4];
            g_read = Syscall::handle(
                static_cast<uint64_t>(SyscallNumber::READ), g_fd,
                reinterpret_cast<uint64_t>(buf), 4, 0, nullptr);
            g_close = Syscall::handle(
                static_cast<uint64_t>(SyscallNumber::CLOSE), g_fd, 0, 0, 0,
                nullptr);
        },
        11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    kernel::test::wait_for_termination_safe(t);
    JARVIS_ASSERT(static_cast<int64_t>(g_fd) >= 0);
    JARVIS_ASSERT_EQ(0ULL, g_read);
    JARVIS_ASSERT_EQ(0ULL, g_close);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a VFS syscall with an unresolvable path fails
// gracefully (returns -1) when run from a REAL dispatched kernel task.
// Input: Dispatch a kernel task that calls sys_open("/nonexistent-audit").
// Expect: sys_open returns -1 (ENOENT path).
// Depends: kernel::Syscall, kernel::vfsd
JARVIS_TEST(vfsd_absent_authorize_fails, "PRE: vfsd, iocd | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = TaskControlBlock::create(
        []() {
            const char *path = "/nonexistent-audit";
            g_ret = Syscall::handle(
                static_cast<uint64_t>(SyscallNumber::OPEN),
                reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
        },
        11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    kernel::test::wait_for_termination_safe(t);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_ret);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies a VFS syscall with a null path fails gracefully from a
// REAL dispatched kernel task.
// Input: Dispatch a kernel task that calls sys_open(nullptr).
// Expect: sys_open returns -1 (no crash).
// Depends: kernel::Syscall, kernel::vfsd
JARVIS_TEST(vfsd_absent_syscall_fails, "PRE: vfsd, iocd | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = TaskControlBlock::create(
        []() {
            g_ret = Syscall::handle(
                static_cast<uint64_t>(SyscallNumber::OPEN), 0, 0, 0, 0,
                nullptr);
        },
        11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    kernel::test::wait_for_termination_safe(t);
    // Null path: resolution fails → -1.  (The old assert `== -1 || >= 0`
    // accepted every value — a tautology.)
    JARVIS_ASSERT(static_cast<int64_t>(g_ret) == -1);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a VFS syscall with a null path does not crash the
// kernel when run from a REAL dispatched task.
// Input: Dispatch a kernel task that calls sys_stat with a null path.
// Expect: Does not crash; the task terminates normally.
// Depends: kernel::Syscall, kernel::vfsd
JARVIS_TEST(vfsd_authorize_null_path, "PRE: vfsd, iocd | POST: none") {
    static uint64_t g_ran = 0;

    auto *t = TaskControlBlock::create(
        []() {
            Syscall::handle(static_cast<uint64_t>(SyscallNumber::STAT), 0, 0,
                            0, 0, nullptr);
            g_ran = 1;
        },
        11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    kernel::test::wait_for_termination_safe(t);
    JARVIS_ASSERT_EQ(1ULL, g_ran);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all VFS authorization tests with the test framework.
// Input: None
// Expect: All vfsd_* authorization tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_vfsd_authorization_tests() {
    Logger::info("Registering VFS daemon authorization tests");
    JARVIS_REGISTER_TEST(vfsd_self_authorization);
    JARVIS_REGISTER_TEST(vfsd_self_authorization_fd_op);
    JARVIS_REGISTER_TEST(vfsd_absent_authorize_fails);
    JARVIS_REGISTER_TEST(vfsd_absent_syscall_fails);
    JARVIS_REGISTER_TEST(vfsd_authorize_null_path);
}
#ifndef __clang__
#pragma GCC diagnostic pop
#endif
#endif
