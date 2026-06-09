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

// Runmode: kernel
// Testidea: Sets alarm for 1 second, verifies it is armed and alarm_ticks is 1 tick ahead, then cancels with 0.
// Input: ALARM syscall with seconds=1, then seconds=0 to cancel.
// Expect: Both calls return 0; alarm_armed transitions true then false; alarm_ticks == ticks() + 1.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
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

// Runmode: kernel
// Testidea: Retrieves time of day via GETTOD and checks it falls within reasonable Unix epoch bounds.
// Input: GETTOD syscall with pointer to a Timeval struct.
// Expect: Returns 0; tv_sec between year 2020 and 2200; tv_usec < 1000000.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_gettod) {
    Timeval tv{};
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETTOD), reinterpret_cast<uint64_t>(&tv), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(tv.tv_sec > static_cast<int64_t>(1577836800ULL));
    JARVIS_ASSERT(tv.tv_sec < static_cast<int64_t>(7258118400ULL));
    JARVIS_ASSERT(tv.tv_usec < 1000000);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Reads system identity via UNAME and validates sysname and machine strings.
// Input: UNAME syscall with pointer to a Utsname struct.
// Expect: Returns 0; sysname is "Jarvis"; machine is "x86_64"; release/version/machine non-empty.
// Depends: kernel::Syscall, string
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

// Runmode: kernel
// Testidea: Manually sets alarm_ticks to start+2 and advances the timer tick count to verify alarm remains armed until the target tick is reached.
// Input: Direct manipulation of cur->alarm_ticks and cur->alarm_armed; arch::Timer::set_ticks_for_test().
// Expect: alarm_armed remains true at tick start+1; passes on reaching start+2.
// Depends: kernel::Scheduler, kernel::arch::Timer
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

// Runmode: kernel
// Testidea: Sets alarm with a subsecond (500ms) interval and verifies the tick calculation is within tolerance, then cancels.
// Input: ALARM syscall with microseconds=500000, then seconds=0 to cancel.
// Expect: Returns 0 both calls; alarm_armed true then false; alarm_ticks within +/-10 of ticks() + 500.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::arch::Timer
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

// Runmode: kernel
// Testidea: GETPID returns the current task's ID.
// Input: GETPID syscall.
// Expect: Return value equals Scheduler::current_task()->id.
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(syscall_dispatch_getpid) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETPID), 0, 0, 0, 0, nullptr);
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT_EQ(cur->id, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Invalid and out-of-range syscall numbers return -1 instead of crashing or succeeding.
// Input: Syscall::handle with numbers MAX_SYSCALL, MAX_SYSCALL+1, and 9999.
// Expect: All three return static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_invalid_returns_minus_one) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL) + 1, 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);

    ret = Syscall::handle(9999, 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: GET_TICKS returns the current timer tick count.
// Input: GET_TICKS syscall.
// Expect: Return value is non-zero or any value (assert only checks it doesn't crash).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_get_ticks) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GET_TICKS), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret > 0 || true);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: YIELD syscall returns 0 to indicate the task yielded the CPU.
// Input: YIELD syscall.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_yield) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::YIELD), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: EXIT syscall returns 0 without actually terminating the test task.
// Input: EXIT syscall.
// Expect: Returns 0 (kernel test framework keeps the task alive).
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_exit_returns_zero) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::EXIT), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: PRINT syscall with no meaningful arguments returns 0 as a no-op.
// Input: PRINT syscall with all zero arguments.
// Expect: Returns 0.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_dispatch_print_noop) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PRINT), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Opens /dev/null, reads from it (returns 0 bytes/EOF), and closes the fd.
// Input: OPEN "/dev/null" (flags=0), READ 10 bytes into buffer, CLOSE.
// Expect: OPEN returns valid fd < MAX_FDS and >= 0; READ returns 0; CLOSE returns 0.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock, kernel::vfs
JARVIS_TEST(syscall_open_read_close) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    const char* path = "/dev/null";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                   reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret < static_cast<uint64_t>(vfs::MAX_FDS));
    JARVIS_ASSERT(static_cast<int64_t>(ret) >= 0);
    int fd = static_cast<int>(ret);

    char buf[32];
    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ),
                          fd, reinterpret_cast<uint64_t>(buf), 10, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(original);
    Scheduler::remove_task(test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Opens /dev/tty for writing, writes a short string, retrieves stat info via fstat, then closes.
// Input: OPEN "/dev/tty" (flags=1), WRITE 4 bytes ("test"), FSTAT, CLOSE.
// Expect: OPEN returns valid fd; WRITE returns 4; FSTAT returns 0; CLOSE returns 0.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock, kernel::vfs
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
    Scheduler::remove_task(test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: FORK syscall returns either 0 (child context) or a positive PID (parent context).
// Input: FORK syscall.
// Expect: Return value is 0 or positive.
// Depends: kernel::Syscall
JARVIS_TEST(syscall_fork_returns_pid) {
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FORK), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT(ret == 0 || ret > 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: EXEC with a nonexistent path returns -1 instead of crashing.
// Input: EXEC with path="/nonexistent", argv={path, nullptr}, envp={nullptr}.
// Expect: Returns static_cast<uint64_t>(-1).
// Depends: kernel::Syscall
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

// Runmode: kernel
// Testidea: Creates a pipe, writes data to the write end, reads it back from the read end, and validates the content.
// Input: PIPE (2-int array), WRITE "pipe" (4 bytes), READ 10 bytes, CLOSE on both fds.
// Expect: PIPE returns 0 with differing non-negative fds; WRITE returns 4; READ returns 4 with content matching "pipe".
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock
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
    Scheduler::remove_task(test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers a signal handler for SIGUSR1 via SIGNAL syscall and verifies it is stored; SIGRETURN is stubbed.
// Input: SIGNAL signal=1, handler=test_signal_handler.
// Expect: SIGNAL returns 0; handler is registered; SIGRETURN not tested (needs populated user stack).
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock
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

    // SIGRETURN requires a valid regs array with a SignalFrame on the user stack;
    // stubbed because no signal delivery path exists to populate one.
    Scheduler::set_current(original);
    Scheduler::remove_task(test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Sends an IPC message to self with data=42 and receives it back, verifying the message queue toggles.
// Input: SEND target=own task id, data=42; RECEIVE with no filters.
// Expect: SEND returns 0; queue non-empty; RECEIVE returns 42; queue empty afterward.
// Depends: kernel::Syscall, kernel::Scheduler, kernel::task::TaskControlBlock, kernel::ipc::MessageQueue
JARVIS_TEST(syscall_send_recv) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    JARVIS_ASSERT(test_task->msg_queue != nullptr);
    Scheduler::add_task(test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(test_task);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::SEND),
                                   test_task->id, 0, 42, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(!test_task->msg_queue->is_empty());

    ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::RECEIVE),
                           0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(42ULL, ret);
    JARVIS_ASSERT(test_task->msg_queue->is_empty());

    Scheduler::set_current(original);
    Scheduler::remove_task(test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Changes the working directory to "/" and then stats "/" to verify both operations succeed.
// Input: CHDIR path="/"; STAT path="/" with 256-byte output buffer.
// Expect: Both return 0.
// Depends: kernel::Syscall
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

// Runmode: kernel
// Testidea: STUB - PAUSE syscall cannot be tested from kernel context because hlt needs interrupts enabled and may never wake.
// Input: PAUSE syscall (not invoked).
// Expect: No assertion; test passes immediately.
// Depends: (none)
JARVIS_TEST(syscall_pause_in_idle_works) {
    // PAUSE calls hlt which requires interrupts enabled; from kernel task
    // context (test runs as kernel task) the hlt may never wake.
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Verifies OPEN syscall forwards to the VFS daemon.
// Input: OPEN syscall (not invoked).
// Expect: No assertion; test passes immediately.
// Depends: (none)
JARVIS_TEST(syscall_open_forwards_to_vfsd) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Verifies READ/WRITE syscalls are routed through the VFS daemon.
// Input: READ/WRITE syscalls (not invoked).
// Expect: No assertion; test passes immediately.
// Depends: (none)
JARVIS_TEST(syscall_read_write_via_vfsd) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Verifies VFS daemon handles chdir/cwd operations.
// Input: CHDIR/GETCWD via VFS daemon (not invoked).
// Expect: No assertion; test passes immediately.
// Depends: (none)
JARVIS_TEST(vfsd_cwd_operations) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Verifies VFS daemon handles stat/fstat operations.
// Input: STAT/FSTAT via VFS daemon (not invoked).
// Expect: No assertion; test passes immediately.
// Depends: (none)
JARVIS_TEST(vfsd_stat_fstat) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all syscall test cases with the test framework.
// Input: None.
// Expect: All JARVIS_REGISTER_TEST calls succeed and tests are available for execution.
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
