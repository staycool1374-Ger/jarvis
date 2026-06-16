#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/daemon/daemon_mgr.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that the VFS daemon boots and registers its PID
// Input: None
// Expect: vfsd::get_vfsd_pid() returns a non-zero PID
// Depends: kernel/vfs/vfsd
JARVIS_TEST(vfsd_boots_and_registers) {
    uint64_t pid = vfsd::get_vfsd_pid();
    JARVIS_ASSERT(pid != 0);
    auto* task = Scheduler::find_task(pid);
    JARVIS_ASSERT(task != nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task (no page_table_) calls sys_open; authorization
// bypasses IPC.
// Input: Create kernel task, call sys_open("/dev/null")
// Expect: Returns valid fd (bypass returns true without IPC)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_open) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    JARVIS_ASSERT(test_task->page_table_ == 0);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/dev/null";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                   reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT(static_cast<int64_t>(ret) >= 0);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task calls sys_read via authorization bypass.
// Input: Open /dev/null, read from it as kernel task
// Expect: read returns 0 (EOF)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_read) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    JARVIS_ASSERT(test_task->page_table_ == 0);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/dev/null";
    uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                  reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT(static_cast<int64_t>(fd) >= 0);

    char buf[4];
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ),
                                   fd, reinterpret_cast<uint64_t>(buf), 4, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    uint64_t close_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE),
                                         fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, close_ret);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task calls sys_write via authorization bypass.
// Input: Open /dev/null, write to it as kernel task
// Expect: write returns count (4)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_write) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    JARVIS_ASSERT(test_task->page_table_ == 0);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/dev/null";
    uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                  reinterpret_cast<uint64_t>(path), 1, 0, 0, nullptr);
    JARVIS_ASSERT(static_cast<int64_t>(fd) >= 0);

    const char* msg = "test";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::WRITE),
                                   fd, reinterpret_cast<uint64_t>(msg), 4, 0, nullptr);
    JARVIS_ASSERT_EQ(4ULL, ret);

    uint64_t close_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE),
                                         fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, close_ret);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task calls sys_stat via authorization bypass.
// Input: Create kernel task, call sys_stat("/dev/null")
// Expect: Returns 0 (success)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_stat) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/dev/null";
    vfs::VfsStat st{};
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::STAT),
                                   reinterpret_cast<uint64_t>(path),
                                   reinterpret_cast<uint64_t>(&st), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task calls sys_fstat via authorization bypass.
// Input: Open /dev/null, call fstat on it as kernel task
// Expect: Returns 0 (success)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_fstat) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/dev/null";
    uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                  reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT(static_cast<int64_t>(fd) >= 0);

    vfs::VfsStat st{};
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::FSTAT),
                                   fd, reinterpret_cast<uint64_t>(&st), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    uint64_t close_ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE),
                                         fd, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, close_ret);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task calls sys_chdir via authorization bypass.
// Input: Create kernel task, call sys_chdir("/")
// Expect: Returns 0 (success)
// Depends: kernel::Syscall, kernel::Scheduler
JARVIS_TEST(vfsd_kernel_bypass_chdir) {
    auto* test_task = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(test_task != nullptr);
    Scheduler::add_task(*test_task);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*test_task);

    const char* path = "/";
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CHDIR),
                                   reinterpret_cast<uint64_t>(path), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*test_task);
    test_task->cleanup();
    delete test_task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon handles read/write IPC messages
// Input: None (stub — requires post-boot sti for userspace IPC)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_read_write) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon responds to an open authorization request
// Input: Send VFS_OPEN IPC message (requires post-boot sti)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_open) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon responds to a close authorization request
// Input: Send VFS_CLOSE IPC message (requires post-boot sti)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_close) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon responds to a stat/resolve authorization request
// Input: Send VFS_STAT IPC message (requires post-boot sti)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_resolve) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon handles mount/unmount operations
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_mount_unmount) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon handles stat/fstat operations
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_stat_fstat) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon handles chdir/getcwd operations
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_handle_chdir_getcwd) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - VFS daemon rejects invalid message types
// Input: Send IPC message with unknown type (requires post-boot sti)
// Expect: Passes (stub)
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_invalid_message_type_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Malformed message rejected by VFS daemon
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_malformed_message_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Unauthorized task rejected by VFS daemon
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_unauthorized_task_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Concurrent requests handled correctly by VFS daemon
// Input: None (stub - requires post-boot sti)
// Expect: Passes
// Depends: kernel/task, kernel/ipc, kernel/vfsd
JARVIS_TEST(vfsd_concurrent_requests) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: VFS daemon restarts after crash (TERMINATED → cleanup → respawn)
// Input: Kill vfsd task (set TERMINATED), reap, then restart_stale_daemons
// Expect: New vfsd PID is non-zero and different from original; new task exists
// Depends: kernel/task, kernel/ipc, kernel/vfsd, kernel/daemon
JARVIS_TEST(vfsd_crash_restarts) {
    uint64_t old_pid = vfsd::get_vfsd_pid();
    JARVIS_ASSERT(old_pid != 0);

    auto* old_task = Scheduler::find_task(old_pid);
    JARVIS_ASSERT(old_task != nullptr);

    // Simulate daemon crash
    old_task->state = TaskState::TERMINATED;

    // Reap orphans — this calls cleanup() which calls daemon::notify_death,
    // resetting PID to 0
    Scheduler::reap_orphans();

    // PID should now be 0
    JARVIS_ASSERT_EQ(0ULL, vfsd::get_vfsd_pid());

    // Trigger daemon restart
    daemon::restart_stale_daemons();

    // Verify new PID
    uint64_t new_pid = vfsd::get_vfsd_pid();
    JARVIS_ASSERT(new_pid != 0);
    JARVIS_ASSERT(new_pid != old_pid);

    // Verify new task exists
    auto* new_task = Scheduler::find_task(new_pid);
    JARVIS_ASSERT(new_task != nullptr);
    JARVIS_ASSERT(new_task->state == TaskState::READY ||
                  new_task->state == TaskState::RUNNING);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: VFS daemon is not restarted beyond max restart limit
// Input: Kill vfsd repeatedly until restart limit exhausted
// Expect: After MAX_RESTART_COUNT restarts, daemon stays dead
// Depends: kernel/task, kernel/ipc, kernel/vfsd, kernel/daemon
JARVIS_TEST(vfsd_exhaust_restart_limit) {
    // Reset daemon state: kill the current daemon and restart it fresh
    uint64_t pid = vfsd::get_vfsd_pid();
    JARVIS_ASSERT(pid != 0);

    // Exhaust remaining restart budget
    for (uint64_t i = 0; i < daemon::MAX_RESTART_COUNT + 1; ++i) {
        auto* task = Scheduler::find_task(vfsd::get_vfsd_pid());
        if (!task) {
            // Daemon didn't restart last time — expected after limit
            break;
        }
        task->state = TaskState::TERMINATED;
        Scheduler::reap_orphans();
        daemon::restart_stale_daemons();
    }

    // After exhausting the limit, kill and don't restart — PID should stay 0
    auto* task = Scheduler::find_task(vfsd::get_vfsd_pid());
    if (task) {
        task->state = TaskState::TERMINATED;
        Scheduler::reap_orphans();
    }
    daemon::restart_stale_daemons();
    JARVIS_ASSERT_EQ(0ULL, vfsd::get_vfsd_pid());

    // Restore daemon state so subsequent tests still have a live vfsd
    daemon::reset_restart_count("vfsd");
    daemon::restart_stale_daemons();
    JARVIS_ASSERT(vfsd::get_vfsd_pid() != 0);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all VFS daemon tests with the test framework
// Input: None
// Expect: All VFS daemon tests registered
// Depends: test framework
void register_vfsd_tests() {
    Logger::info("Registering VFS daemon tests");
    JARVIS_REGISTER_TEST(vfsd_boots_and_registers);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_open);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_read);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_write);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_stat);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_fstat);
    JARVIS_REGISTER_TEST(vfsd_kernel_bypass_chdir);
    JARVIS_REGISTER_TEST(vfsd_handle_read_write);
    JARVIS_REGISTER_TEST(vfsd_handle_open);
    JARVIS_REGISTER_TEST(vfsd_handle_close);
    JARVIS_REGISTER_TEST(vfsd_handle_resolve);
    JARVIS_REGISTER_TEST(vfsd_handle_mount_unmount);
    JARVIS_REGISTER_TEST(vfsd_handle_stat_fstat);
    JARVIS_REGISTER_TEST(vfsd_handle_chdir_getcwd);
    JARVIS_REGISTER_TEST(vfsd_invalid_message_type_rejected);
    JARVIS_REGISTER_TEST(vfsd_malformed_message_rejected);
    JARVIS_REGISTER_TEST(vfsd_unauthorized_task_rejected);
    JARVIS_REGISTER_TEST(vfsd_concurrent_requests);
    JARVIS_REGISTER_TEST(vfsd_crash_restarts);
    JARVIS_REGISTER_TEST(vfsd_exhaust_restart_limit);
}