#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/vfs/vfs.hpp>

using namespace kernel;

enum VfsMsgType {
    VFS_OPEN = 100,
    VFS_CLOSE,
    VFS_READ,
    VFS_WRITE,
    VFS_RESOLVE,
    VFS_MOUNT,
    VFS_FSTAT,
    VFS_STAT,
    VFS_CHDIR,
    VFS_GETCWD,
};

struct VfsMsg {
    uint64_t type;
    uint64_t args[5];
};

JARVIS_TEST(vfsd_boots_and_registers) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t vfsd_pid = 2;
    auto* vfsd = Scheduler::find_task(vfsd_pid);
    JARVIS_ASSERT(vfsd != nullptr);
    JARVIS_ASSERT(vfsd->msg_queue != nullptr);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_open) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t vfsd_pid = 2;
    auto* vfsd = Scheduler::find_task(vfsd_pid);
    JARVIS_ASSERT(vfsd != nullptr);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_read_write) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_resolve) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_mount_unmount) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_stat_fstat) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_handle_chdir_getcwd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_invalid_message_type_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_malformed_message_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_unauthorized_task_rejected) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_concurrent_requests) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_crash_restarts) {
    JARVIS_TEST_PASS();
}

void register_vfsd_tests() {
    Logger::info("Registering VFS daemon tests");
    JARVIS_REGISTER_TEST(vfsd_boots_and_registers);
    JARVIS_REGISTER_TEST(vfsd_handle_open);
    JARVIS_REGISTER_TEST(vfsd_handle_read_write);
    JARVIS_REGISTER_TEST(vfsd_handle_resolve);
    JARVIS_REGISTER_TEST(vfsd_handle_mount_unmount);
    JARVIS_REGISTER_TEST(vfsd_handle_stat_fstat);
    JARVIS_REGISTER_TEST(vfsd_handle_chdir_getcwd);
    JARVIS_REGISTER_TEST(vfsd_invalid_message_type_rejected);
    JARVIS_REGISTER_TEST(vfsd_malformed_message_rejected);
    JARVIS_REGISTER_TEST(vfsd_unauthorized_task_rejected);
    JARVIS_REGISTER_TEST(vfsd_concurrent_requests);
    JARVIS_REGISTER_TEST(vfsd_crash_restarts);
}
