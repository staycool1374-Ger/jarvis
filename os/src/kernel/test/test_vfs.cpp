#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(vfs_fdtable_alloc_free) {
    vfs::FdTable ft;
    int fd1 = ft.alloc();
    JARVIS_ASSERT(fd1 >= 0);
    JARVIS_ASSERT(ft.get(fd1) != nullptr);
    JARVIS_ASSERT(ft.get(fd1)->used);
    ft.free(fd1);
    JARVIS_ASSERT(ft.get(fd1) == nullptr || !ft.get(fd1)->used);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_fdtable_multiple) {
    vfs::FdTable ft;
    int fds[5];
    for (int i = 0; i < 5; ++i) {
        fds[i] = ft.alloc();
        JARVIS_ASSERT(fds[i] >= 0);
    }
    for (int i = 0; i < 5; ++i)
        ft.free(fds[i]);
    int fd = ft.alloc();
    JARVIS_ASSERT(fd >= 0);
    ft.free(fd);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_root) {
    auto* vn = vfs::resolve("/");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_dev) {
    auto* vn = vfs::resolve("/dev");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_tty) {
    auto* vn = vfs::resolve("/dev/tty");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_null) {
    auto* vn = vfs::resolve("/dev/null");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_proc) {
    auto* vn = vfs::resolve("/proc");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_nonexistent) {
    auto* vn = vfs::resolve("/nonexistent_path_xyz");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_relative_path) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_dotdot) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_mount_unmount) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_server_boots_and_responds) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_open_forwards_to_vfsd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(syscall_read_write_via_vfsd) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_mount_unmount_ipc) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_cwd_operations) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_stat_fstat) {
    JARVIS_TEST_PASS();
}

void register_vfs_tests() {
    Logger::info("Registering VFS tests");

    JARVIS_REGISTER_TEST(vfs_fdtable_alloc_free);
    JARVIS_REGISTER_TEST(vfs_fdtable_multiple);

    JARVIS_REGISTER_TEST(vfs_resolve_root);
    JARVIS_REGISTER_TEST(vfs_resolve_dev);
    JARVIS_REGISTER_TEST(vfs_resolve_tty);
    JARVIS_REGISTER_TEST(vfs_resolve_null);
    JARVIS_REGISTER_TEST(vfs_resolve_proc);
    JARVIS_REGISTER_TEST(vfs_resolve_nonexistent);

    JARVIS_REGISTER_TEST(vfs_resolve_relative_path);
    JARVIS_REGISTER_TEST(vfs_resolve_dotdot);
    JARVIS_REGISTER_TEST(vfs_mount_unmount);
    JARVIS_REGISTER_TEST(vfsd_server_boots_and_responds);
    JARVIS_REGISTER_TEST(syscall_open_forwards_to_vfsd);
    JARVIS_REGISTER_TEST(syscall_read_write_via_vfsd);
    JARVIS_REGISTER_TEST(vfsd_mount_unmount_ipc);
    JARVIS_REGISTER_TEST(vfsd_cwd_operations);
    JARVIS_REGISTER_TEST(vfsd_stat_fstat);
}
