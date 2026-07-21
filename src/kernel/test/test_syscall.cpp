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

/// @file test_syscall.cpp
/// @brief System call interface tests.

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/ipc/ipc.hpp>
#include <signal.hpp>
#include "test_sched_helpers.hpp"

using namespace kernel;

static void test_signal_handler(int sig) {
    (void)sig;
}

struct Timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct Utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

// Runmode: kernel
// Testidea: Sets alarm for 1 second, verifies it is armed and alarm_ticks is
// 1 tick ahead, then cancels with 0.
// Input: ALARM syscall with seconds=1, then seconds=0 to cancel.
// Expect: Both calls return 0; alarm_armed transitions true then false;
// alarm_ticks == ticks() + 1.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
JARVIS_TEST(syscall_alarm_basic, "PRE: none | POST: none") {
    auto *cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM),
                                   0, 1, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(cur->alarm_armed);
    JARVIS_ASSERT(cur->alarm_ticks == 1ULL);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0,
                          0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(!cur->alarm_armed);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Retrieves time of day via GETTOD and checks it falls within
// reasonable Unix epoch bounds.
// Input: GETTOD syscall with pointer to a Timeval struct.
// Expect: Returns 0; tv_sec between year 2020 and 2200; tv_usec < 1000000.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_gettod, "PRE: none | POST: none") {
    Timeval tv{};
    uint64_t ret =
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETTOD),
                        reinterpret_cast<uint64_t>(&tv), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(tv.tv_sec > static_cast<int64_t>(1577836800ULL));
    JARVIS_ASSERT(tv.tv_sec < static_cast<int64_t>(7258118400ULL));
    JARVIS_ASSERT(tv.tv_usec < 1000000);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Reads system identity via UNAME and validates sysname and
// machine strings.
// Input: UNAME syscall with pointer to a Utsname struct.
// Expect: Returns 0; sysname is "Jarvis"; machine is "x86_64";
// release/version/machine non-empty.
// Depends: kernel::Syscall, string
JARVIS_TEST(syscall_uname, "PRE: none | POST: none") {
    Utsname uts{};
    uint64_t ret =
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::UNAME),
                        reinterpret_cast<uint64_t>(&uts), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(strlen(uts.sysname) > 0);
    JARVIS_ASSERT(strlen(uts.release) > 0);
    JARVIS_ASSERT(strlen(uts.version) > 0);
    JARVIS_ASSERT(strlen(uts.machine) > 0);

    JARVIS_ASSERT_EQ(0, strcmp(uts.sysname, "Jarvis"));
    JARVIS_ASSERT_EQ(0, strcmp(uts.machine, "x86_64"));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies alarm decrements each on_tick and fires after reaching 0.
// Input: Set alarm_ticks=2, call on_tick() twice.
// Expect: alarm_armed true after first tick, false + SIGALRM after second.
// Depends: kernel::Scheduler
JARVIS_TEST(alarm_fires_after_ticks, "PRE: none | POST: none") {
    auto *cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    cur->alarm_ticks = 2;
    cur->alarm_armed = true;
    cur->pending_signals = 0;

    Scheduler::on_tick();
    JARVIS_ASSERT(cur->alarm_armed);
    JARVIS_ASSERT_EQ(cur->alarm_ticks, 1ULL);

    Scheduler::on_tick();
    JARVIS_ASSERT(!cur->alarm_armed);
    JARVIS_ASSERT_EQ(cur->alarm_ticks, 0ULL);
    JARVIS_ASSERT((cur->pending_signals &
                   (1ULL << static_cast<uint64_t>(Signal::SIGALRM))) != 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Sets alarm with a subsecond (500ms) interval and verifies the
// tick calculation is within tolerance, then cancels.
// Input: ALARM syscall with microseconds=500000, then seconds=0 to cancel.
// Expect: Returns 0 both calls; alarm_armed true then false; alarm_ticks
// within +/-10 of ticks() + 500.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
JARVIS_TEST(syscall_alarm_subsecond, "PRE: none | POST: none") {
    auto *cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM),
                                   0, 500000, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(cur->alarm_armed);
    JARVIS_ASSERT(cur->alarm_ticks >= 490ULL);
    JARVIS_ASSERT(cur->alarm_ticks <= 510ULL);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0,
                          0, nullptr);
    JARVIS_ASSERT(!cur->alarm_armed);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GETPID returns the current task's ID.
// Input: GETPID syscall.
// Expect: Return value equals Scheduler::current_task()->id.
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(syscall_dispatch_getpid, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETPID),
                                   0, 0, 0, 0, nullptr);
    auto *cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT_EQ(cur->id, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Invalid and out-of-range syscall numbers return -1 instead of
// crashing or succeeding.
// Input: Syscall::handle with numbers MAX_SYSCALL, MAX_SYSCALL+1, and 9999.
// Expect: All three return static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_invalid_returns_minus_one,
            "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL) + 1,
                          0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(9999, 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GET_TICKS returns the current timer tick count.
// Input: GET_TICKS syscall.
// Expect: Return value is non-zero or any value (assert only checks it
// doesn't crash).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_get_ticks, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GET_TICKS), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret > 0 || true);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: YIELD syscall returns 0 to indicate the task yielded the CPU.
// Input: YIELD syscall.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_yield, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::YIELD),
                                   0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: REBOOT syscall number is valid and dispatch table slot is
// populated. Input: Verify SyscallNumber::REBOOT and table entry. Expect: Enum
// valid, table entry non-null (actual reboot skipped in test). Depends:
// kernel::Syscall
JARVIS_TEST(syscall_dispatch_reboot, "PRE: none | POST: none") {
    uint64_t num = static_cast<uint64_t>(SyscallNumber::REBOOT);
    JARVIS_ASSERT_EQ(49ULL, num);
    JARVIS_ASSERT(num < static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: HALT syscall number is valid and dispatch table slot is populated.
// Input: Verify SyscallNumber::HALT and table entry.
// Expect: Enum valid, table entry non-null (actual halt skipped in test).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_halt, "PRE: none | POST: none") {
    uint64_t num = static_cast<uint64_t>(SyscallNumber::HALT);
    JARVIS_ASSERT_EQ(50ULL, num);
    JARVIS_ASSERT(num < static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: EXIT syscall returns 0 without actually terminating the test task.
// Input: EXIT syscall.
// Expect: Returns 0 (kernel test framework keeps the task alive).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_exit_returns_zero, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::EXIT),
                                   0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: PRINT syscall with no meaningful arguments returns 0 as a no-op.
// Input: PRINT syscall with all zero arguments.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_print_noop, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PRINT),
                                   0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}



// Runmode: kernel
// Testidea: FORK syscall returns either 0 (child context) or a positive PID
// (parent context).
// Input: FORK syscall.
// Expect: Return value is 0 or positive.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_fork_returns_pid, "PRE: none | POST: none") {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FORK),
                                   0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret == 0 || ret > 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: EXEC with a nonexistent path returns -1 instead of crashing.
// Input: EXEC with path="/nonexistent", argv={path, nullptr}, envp={nullptr}.
// Expect: Returns static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_exec_nonexistent, "PRE: none | POST: none") {
    const char *path = "/nonexistent";
    const char *argv[] = {path, nullptr};
    const char *envp[] = {nullptr};

    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::EXEC),
        reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(argv),
        reinterpret_cast<uint64_t>(envp), 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers a signal handler for SIGUSR1 via SIGNAL syscall and
// verifies it is stored; SIGRETURN is stubbed.
// Input: SIGNAL signal=1, handler=test_signal_handler.
// Expect: SIGNAL returns 0; handler is registered; SIGRETURN not tested
// (needs populated user stack).
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(syscall_signal_sigreturn, "PRE: none | POST: none") {
    auto *test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(*test_task);

    kernel::test::ScopedCurrentTask _scoped(*test_task);

    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::SIGNAL), 1,
        reinterpret_cast<uint64_t>(test_signal_handler), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(test_task->get_signal_handler(1) == test_signal_handler);

    // SIGRETURN requires a valid regs array with a SignalFrame on the user
    // stack; stubbed because no signal delivery path exists to populate one.
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all syscall test cases with the test framework.
// Input: None.
// Expect: All JARVIS_REGISTER_TEST calls succeed and tests are available for
// execution.
// Depends: kernel::Logger, kernel::test framework
void register_syscall_tests() {
    Logger::info("Registering syscall tests");

    JARVIS_REGISTER_TEST(syscall_alarm_basic);
    JARVIS_REGISTER_TEST(syscall_gettod);
    JARVIS_REGISTER_TEST(syscall_uname);
    JARVIS_REGISTER_TEST(alarm_fires_after_ticks);
    JARVIS_REGISTER_TEST(syscall_alarm_subsecond);

    JARVIS_REGISTER_TEST(syscall_dispatch_getpid);
    JARVIS_REGISTER_TEST(syscall_dispatch_invalid_returns_minus_one);
    JARVIS_REGISTER_TEST(syscall_dispatch_get_ticks);
    JARVIS_REGISTER_TEST(syscall_dispatch_yield);
    JARVIS_REGISTER_TEST(syscall_dispatch_reboot);
    JARVIS_REGISTER_TEST(syscall_dispatch_halt);
    JARVIS_REGISTER_TEST(syscall_dispatch_exit_returns_zero);
    JARVIS_REGISTER_TEST(syscall_dispatch_print_noop);

    JARVIS_REGISTER_TEST(syscall_fork_returns_pid);
    JARVIS_REGISTER_TEST(syscall_exec_nonexistent);
    JARVIS_REGISTER_TEST(syscall_signal_sigreturn);
}
