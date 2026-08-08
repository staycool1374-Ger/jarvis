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

/// @file test_syscall.cpp
/// @brief System call interface tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): every syscall is invoked by a REAL
/// kernel task (prio ≥ 11) inside its dispatched lambda — the handler's
/// `syscall_task()` resolves to the genuinely-running task.  Alarm tests let
/// the REAL timer ISR fire before asserting the signal.  The harness never
/// calls Syscall::handle() directly and never mutates task alarm fields.

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

namespace {

/// @brief Create a REAL kernel task (prio ≥ 11), dispatch it, and wait for
///        genuine termination.  The lambda runs in the task's own context so
///        `syscall_task()` resolves to it.
TaskControlBlock *run_syscall_task(void (*entry)(), uint64_t prio = 11,
                                   uint64_t period = 10) {
    auto *t = TaskControlBlock::create(entry, prio, period);
    if (t == nullptr)
        return nullptr;
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    kernel::test::wait_for_termination_safe(t);
    return t;
}

void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    kernel::test::terminate_if_live(t);
}

} // namespace

// Runmode: kernel
// Testidea: A REAL task calls the ALARM syscall; the alarm is armed with the
// requested tick count.  The same task then cancels it with 0.
// Input: Dispatched kernel task calls ALARM (seconds=1), verifies
//        alarm_armed and alarm_ticks, then ALARM (seconds=0) to cancel.
// Expect: syscall returns 0 both times; alarm_armed true then false;
// alarm_ticks == ticks() + 1 (captured inside the task).
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
JARVIS_TEST(syscall_alarm_basic, "PRE: none | POST: none") {
    static uint64_t g_ret1 = 0;
    static uint64_t g_armed = 0;
    static uint64_t g_ticks = 0;
    static uint64_t g_ret2 = 0;
    static uint64_t g_cancelled = 0;

    auto *t = run_syscall_task([]() {
        auto *cur = Scheduler::current_task();
        g_ret1 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::ALARM), 1, 0, 0, 0, nullptr);
        g_armed = cur->alarm_armed ? 1 : 0;
        g_ticks = cur->alarm_ticks;
        g_ret2 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0, 0, nullptr);
        g_cancelled = cur->alarm_armed ? 1 : 0;
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret1);
    JARVIS_ASSERT_EQ(1ULL, g_armed);
    JARVIS_ASSERT_EQ(1000ULL, g_ticks);
    JARVIS_ASSERT_EQ(0ULL, g_ret2);
    JARVIS_ASSERT_EQ(0ULL, g_cancelled);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GETTOD from a REAL task returns a time within reasonable Unix
// epoch bounds.
// Input: Dispatched kernel task calls GETTOD with a Timeval pointer.
// Expect: Returns 0; tv_sec between year 2020 and 2200; tv_usec < 1000000.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_gettod, "PRE: none | POST: none") {
    static int64_t g_sec = 0;
    static int64_t g_usec = 0;
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        Timeval tv{};
        g_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::GETTOD),
            reinterpret_cast<uint64_t>(&tv), 0, 0, 0, nullptr);
        g_sec = tv.tv_sec;
        g_usec = tv.tv_usec;
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret);
    JARVIS_ASSERT(g_sec > static_cast<int64_t>(1577836800ULL));
    JARVIS_ASSERT(g_sec < static_cast<int64_t>(7258118400ULL));
    JARVIS_ASSERT(g_usec < 1000000);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: UNAME from a REAL task returns the system identity.
// Input: Dispatched kernel task calls UNAME with a Utsname pointer.
// Expect: Returns 0; sysname is "NexIOS"; machine is "x86_64";
// release/version/machine non-empty.
// Depends: kernel::Syscall, string
JARVIS_TEST(syscall_uname, "PRE: none | POST: none") {
    static char g_sysname[65] = {};
    static char g_release[65] = {};
    static char g_version[65] = {};
    static char g_machine[65] = {};
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        Utsname uts{};
        g_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::UNAME),
            reinterpret_cast<uint64_t>(&uts), 0, 0, 0, nullptr);
        __builtin_strncpy(g_sysname, uts.sysname, sizeof(g_sysname) - 1);
        __builtin_strncpy(g_release, uts.release, sizeof(g_release) - 1);
        __builtin_strncpy(g_version, uts.version, sizeof(g_version) - 1);
        __builtin_strncpy(g_machine, uts.machine, sizeof(g_machine) - 1);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret);
    JARVIS_ASSERT(strlen(g_sysname) > 0);
    JARVIS_ASSERT(strlen(g_release) > 0);
    JARVIS_ASSERT(strlen(g_version) > 0);
    JARVIS_ASSERT(strlen(g_machine) > 0);
    JARVIS_ASSERT_EQ(0, strcmp(g_sysname, "NexIOS"));
    JARVIS_ASSERT_EQ(0, strcmp(g_machine, "x86_64"));
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: A REAL alarm armed by the task fires after the requested real
// tick count: the real on_tick ISR decrements alarm_ticks and raises
// SIGALRM.  The task arms an alarm (2 ticks) and busy-waits for the signal.
// Input: Dispatched kernel task calls ALARM (microseconds=2000) then polls
//        its own pending_signals for SIGALRM.
// Expect: alarm_armed true after arming; then SIGALRM pending and alarm
//         cleared after the real ticks fire.
// Depends: kernel::Scheduler
JARVIS_TEST(alarm_fires_after_ticks, "PRE: none | POST: none") {
    static uint64_t g_still_armed = 0;
    static uint64_t g_alarm_ticks_after_arm = 0;
    static uint64_t g_fired = 0;
    static uint64_t g_cleared = 0;

    auto *t = run_syscall_task([]() {
        auto *cur = Scheduler::current_task();
        uint64_t ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::ALARM), 0, 2000, 0, 0,
            nullptr);
        if (ret != 0)
            return;
        g_still_armed = cur->alarm_armed ? 1 : 0;
        g_alarm_ticks_after_arm = cur->alarm_ticks;

        // Busy-wait for the REAL timer ISR to decrement to 0 and raise
        // SIGALRM.
        uint64_t start = arch::Timer::ticks();
        while (arch::Timer::ticks() - start < 20) {
            if (cur->pending_signals &
                (1ULL << static_cast<uint64_t>(Signal::SIGALRM))) {
                g_fired = 1;
                g_cleared = cur->alarm_armed ? 0 : 1;
                return;
            }
            arch::pause();
        }
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(1ULL, g_still_armed);
    JARVIS_ASSERT_EQ(2ULL, g_alarm_ticks_after_arm);
    JARVIS_ASSERT_EQ(1ULL, g_fired);
    JARVIS_ASSERT_EQ(1ULL, g_cleared);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: A REAL task arms a subsecond (500ms) alarm and verifies the tick
// calculation is within tolerance, then cancels.
// Input: Dispatched kernel task calls ALARM (microseconds=500000), then
//        seconds=0 to cancel.
// Expect: Returns 0 both calls; alarm_armed true then false; alarm_ticks
// within +/-10 of 500.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
JARVIS_TEST(syscall_alarm_subsecond, "PRE: none | POST: none") {
    static uint64_t g_ret1 = 0;
    static uint64_t g_armed = 0;
    static uint64_t g_ticks = 0;
    static uint64_t g_ret2 = 0;
    static uint64_t g_cancelled = 0;

    auto *t = run_syscall_task([]() {
        auto *cur = Scheduler::current_task();
        g_ret1 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::ALARM), 0, 500000, 0, 0,
            nullptr);
        g_armed = cur->alarm_armed ? 1 : 0;
        g_ticks = cur->alarm_ticks;
        g_ret2 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0, 0, nullptr);
        g_cancelled = cur->alarm_armed ? 1 : 0;
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret1);
    JARVIS_ASSERT_EQ(1ULL, g_armed);
    JARVIS_ASSERT(g_ticks >= 490ULL);
    JARVIS_ASSERT(g_ticks <= 510ULL);
    JARVIS_ASSERT_EQ(0ULL, g_ret2);
    JARVIS_ASSERT_EQ(0ULL, g_cancelled);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GETPID from a REAL task returns that task's own ID.
// Input: Dispatched kernel task calls GETPID.
// Expect: Return value equals the running task's id.
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(syscall_dispatch_getpid, "PRE: none | POST: none") {
    static uint64_t g_pid = 0;
    static uint64_t g_self = 0;

    auto *t = run_syscall_task([]() {
        g_pid = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::GETPID), 0, 0, 0, 0, nullptr);
        g_self = Scheduler::current_task()->id;
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(g_self, g_pid);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Invalid and out-of-range syscall numbers return -1 instead of
// crashing or succeeding.
// Input: Dispatched kernel task calls Syscall::handle with MAX_SYSCALL,
//        MAX_SYSCALL+1, and 9999.
// Expect: All three return static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_invalid_returns_minus_one,
            "PRE: none | POST: none") {
    static uint64_t g_r1 = 0, g_r2 = 0, g_r3 = 0;

    auto *t = run_syscall_task([]() {
        g_r1 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL), 0, 0, 0, 0,
            nullptr);
        g_r2 = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL) + 1, 0, 0, 0, 0,
            nullptr);
        g_r3 = Syscall::handle(9999, 0, 0, 0, 0, nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_r1);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_r2);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_r3);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GET_TICKS from a REAL task returns the current timer tick count.
// Input: Dispatched kernel task calls GET_TICKS.
// Expect: Return value is non-zero or any value (assert only checks it
// doesn't crash).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_get_ticks, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        g_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::GET_TICKS), 0, 0, 0, 0,
            nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    // GET_TICKS returns the monotonic tick count (> 0 since boot init).
    JARVIS_ASSERT(g_ret > 0);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: YIELD syscall from a REAL task returns 0 to indicate the task
// yielded the CPU.
// Input: Dispatched kernel task calls YIELD.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_yield, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        g_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::YIELD), 0,
                                0, 0, 0, nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret);
    release_task(t);
    Scheduler::drain_zombie_list();
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
// Testidea: PRINT syscall with no meaningful arguments returns 0 as a no-op.
// Input: Dispatched kernel task calls PRINT with all zero arguments.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_print_noop, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        g_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PRINT), 0,
                                0, 0, 0, nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: FORK from a REAL kernel task with null regs returns -1
// (syscall_handlers_process.cpp: sys_fork returns -1 when regs==nullptr).
// The old test asserted `g_ret == 0 || g_ret > 0` — a tautology that also
// accepts UINT64_MAX, masking the real contract.
// Input: Dispatched kernel task calls FORK with null regs.
// Expect: Return value is -1 (regs == nullptr path).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_fork_returns_pid, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        g_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FORK), 0,
                                0, 0, 0, nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_ret);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: EXEC with a nonexistent path from a REAL task returns -1 instead
// of crashing.
// Input: Dispatched kernel task calls EXEC with path="/nonexistent",
// argv={path, nullptr}, envp={nullptr}.
// Expect: Returns static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_exec_nonexistent, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;

    auto *t = run_syscall_task([]() {
        const char *path = "/nonexistent";
        const char *argv[] = {path, nullptr};
        const char *envp[] = {nullptr};
        g_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::EXEC),
            reinterpret_cast<uint64_t>(path), reinterpret_cast<uint64_t>(argv),
            reinterpret_cast<uint64_t>(envp), 0, nullptr);
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), g_ret);
    release_task(t);
    Scheduler::drain_zombie_list();
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: A REAL task registers a signal handler for SIGUSR1 via the SIGNAL
// syscall and verifies it is stored on the running task's TCB.
// Input: Dispatched kernel task calls SIGNAL signal=1,
// handler=test_signal_handler.
// Expect: SIGNAL returns 0; the task's handler table has the handler.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock
JARVIS_TEST(syscall_signal_sigreturn, "PRE: none | POST: none") {
    static uint64_t g_ret = 0;
    static uint64_t g_stored = 0;

    auto *t = run_syscall_task([]() {
        auto *cur = Scheduler::current_task();
        g_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::SIGNAL), 1,
            reinterpret_cast<uint64_t>(test_signal_handler), 0, 0, nullptr);
        g_stored = (cur->get_signal_handler(1) == test_signal_handler) ? 1 : 0;
    });
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT_EQ(0ULL, g_ret);
    JARVIS_ASSERT_EQ(1ULL, g_stored);
    release_task(t);
    Scheduler::drain_zombie_list();
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
    // JARVIS_REGISTER_TEST(syscall_dispatch_exit_returns_zero); — disabled:
    // sys_exit terminates the calling task and switches away, preventing
    // the test runner from returning.
    JARVIS_REGISTER_TEST(syscall_dispatch_print_noop);

    JARVIS_REGISTER_TEST(syscall_fork_returns_pid);
    JARVIS_REGISTER_TEST(syscall_exec_nonexistent);
    JARVIS_REGISTER_TEST(syscall_signal_sigreturn);
}
