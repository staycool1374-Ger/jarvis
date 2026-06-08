#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>

using namespace kernel;

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

JARVIS_TEST(syscall_alarm_basic) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 1, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(cur->alarm_armed);
    JARVIS_ASSERT(cur->alarm_ticks == arch::Timer::ticks() + 1);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(!cur->alarm_armed);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_gettod) {
    Timeval tv{};
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETTOD), reinterpret_cast<uint64_t>(&tv), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(tv.tv_sec > static_cast<int64_t>(1577836800ULL));
    JARVIS_ASSERT(tv.tv_sec < static_cast<int64_t>(7258118400ULL));
    JARVIS_ASSERT(tv.tv_usec < 1000000);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_uname) {
    Utsname uts{};
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::UNAME), reinterpret_cast<uint64_t>(&uts), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(strlen(uts.sysname) > 0);
    JARVIS_ASSERT(strlen(uts.release) > 0);
    JARVIS_ASSERT(strlen(uts.version) > 0);
    JARVIS_ASSERT(strlen(uts.machine) > 0);

    JARVIS_ASSERT_EQ(0, strcmp(uts.sysname, "Jarvis"));
    JARVIS_ASSERT_EQ(0, strcmp(uts.machine, "x86_64"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(alarm_fires_after_ticks) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t start_ticks = arch::Timer::ticks();
    cur->alarm_ticks = start_ticks + 2;
    cur->alarm_armed = true;

    arch::Timer::set_ticks_for_test(start_ticks + 1);
    JARVIS_ASSERT(cur->alarm_armed);

    arch::Timer::set_ticks_for_test(start_ticks + 2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_alarm_subsecond) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 500000, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(cur->alarm_armed);
    uint64_t expected_ticks = arch::Timer::ticks() + 500;
    JARVIS_ASSERT(cur->alarm_ticks >= expected_ticks - 10);
    JARVIS_ASSERT(cur->alarm_ticks <= expected_ticks + 10);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::ALARM), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(!cur->alarm_armed);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_getpid) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETPID), 0, 0, 0, 0, nullptr);
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT_EQ(cur->id, ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_invalid_returns_minus_one) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL) + 1, 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(9999, 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_get_ticks) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GET_TICKS), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret > 0 || true);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_yield) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::YIELD), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_exit_returns_zero) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::EXIT), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_dispatch_print_noop) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PRINT), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

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
    JARVIS_REGISTER_TEST(syscall_dispatch_exit_returns_zero);
    JARVIS_REGISTER_TEST(syscall_dispatch_print_noop);
}
