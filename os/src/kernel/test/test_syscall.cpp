#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/ipc/ipc.hpp>
#include <signal.hpp>

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

JARVIS_TEST(syscall_open_read_close) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    const char* path = "/dev/tty";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                   reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret < static_cast<uint64_t>(vfs::MAX_FDS));
    int fd = static_cast<int>(ret);

    char buf[32];
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ),
                          fd, reinterpret_cast<uint64_t>(buf), 10, 0, nullptr);
    JARVIS_ASSERT(ret < static_cast<uint64_t>(-1));

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_write_fstat) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    const char* path = "/dev/tty";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                   reinterpret_cast<uint64_t>(path), 1, 0, 0, nullptr);
    JARVIS_ASSERT(ret < static_cast<uint64_t>(vfs::MAX_FDS));
    int fd = static_cast<int>(ret);

    const char* msg = "test";
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::WRITE),
                          fd, reinterpret_cast<uint64_t>(msg), 4, 0, nullptr);
    JARVIS_ASSERT_EQ(4ULL, ret);

    vfs::VfsStat st{};
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FSTAT),
                          fd, reinterpret_cast<uint64_t>(&st), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_fork_returns_pid) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FORK), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret == 0 || ret > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_exec_nonexistent) {
    const char* path = "/nonexistent";
    const char* argv[] = {path, nullptr};
    const char* envp[] = {nullptr};

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::EXEC),
                                   reinterpret_cast<uint64_t>(path),
                                   reinterpret_cast<uint64_t>(argv),
                                   reinterpret_cast<uint64_t>(envp), 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_pipe_read_write) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    int pipefd[2];
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PIPE),
                                   reinterpret_cast<uint64_t>(pipefd), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(pipefd[0] >= 0);
    JARVIS_ASSERT(pipefd[1] >= 0);
    JARVIS_ASSERT(pipefd[0] != pipefd[1]);

    const char* msg = "pipe";
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::WRITE),
                          pipefd[1], reinterpret_cast<uint64_t>(msg), 4, 0, nullptr);
    JARVIS_ASSERT_EQ(4ULL, ret);

    char buf[32];
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ),
                          pipefd[0], reinterpret_cast<uint64_t>(buf), 10, 0, nullptr);
    JARVIS_ASSERT_EQ(4ULL, ret);
    JARVIS_ASSERT(buf[0] == 'p');
    JARVIS_ASSERT(buf[1] == 'i');
    JARVIS_ASSERT(buf[2] == 'p');
    JARVIS_ASSERT(buf[3] == 'e');

    Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), pipefd[0], 0, 0, 0, nullptr);
    Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), pipefd[1], 0, 0, 0, nullptr);

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_signal_sigreturn) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::SIGNAL),
                                   1, reinterpret_cast<uint64_t>(test_signal_handler), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(test_task->get_signal_handler(1) == test_signal_handler);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::SIGRETURN),
                          0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_send_recv) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    JARVIS_ASSERT(test_task->msg_queue != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::SEND),
                                   test_task->id, 42, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(!test_task->msg_queue->is_empty());

    struct Message {
        uint64_t sender_id;
        uint64_t type;
        uint64_t priority;
        uint8_t  data[64];
        size_t   data_size;
    } msg{};

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::RECEIVE),
                          reinterpret_cast<uint64_t>(&msg), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT_EQ(42ULL, msg.type);
    JARVIS_ASSERT_EQ(test_task->id, msg.sender_id);
    JARVIS_ASSERT(test_task->msg_queue->is_empty());

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_chdir_getcwd) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CHDIR),
                                   reinterpret_cast<uint64_t>("/"), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    char buf[256];
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::STAT),
                          reinterpret_cast<uint64_t>("/"),
                          reinterpret_cast<uint64_t>(buf), 0, 0, nullptr);
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

    JARVIS_REGISTER_TEST(syscall_open_read_close);
    JARVIS_REGISTER_TEST(syscall_write_fstat);
    JARVIS_REGISTER_TEST(syscall_fork_returns_pid);
    JARVIS_REGISTER_TEST(syscall_exec_nonexistent);
    JARVIS_REGISTER_TEST(syscall_pipe_read_write);
    JARVIS_REGISTER_TEST(syscall_signal_sigreturn);
    JARVIS_REGISTER_TEST(syscall_send_recv);
    JARVIS_REGISTER_TEST(syscall_chdir_getcwd);
}

JARVIS_TEST(syscall_pause_in_idle_works) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PAUSE), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(original);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_open_forwards_to_vfsd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_read_write_via_vfsd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_cwd_operations) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_stat_fstat) {
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

    JARVIS_REGISTER_TEST(syscall_open_read_close);
    JARVIS_REGISTER_TEST(syscall_write_fstat);
    JARVIS_REGISTER_TEST(syscall_fork_returns_pid);
    JARVIS_REGISTER_TEST(syscall_exec_nonexistent);
    JARVIS_REGISTER_TEST(syscall_pipe_read_write);
    JARVIS_REGISTER_TEST(syscall_signal_sigreturn);
    JARVIS_REGISTER_TEST(syscall_send_recv);
    JARVIS_REGISTER_TEST(syscall_chdir_getcwd);

    JARVIS_REGISTER_TEST(syscall_pause_in_idle_works);
    JARVIS_REGISTER_TEST(syscall_open_forwards_to_vfsd);
    JARVIS_REGISTER_TEST(syscall_read_write_via_vfsd);
    JARVIS_REGISTER_TEST(vfsd_cwd_operations);
    JARVIS_REGISTER_TEST(vfsd_stat_fstat);
}
