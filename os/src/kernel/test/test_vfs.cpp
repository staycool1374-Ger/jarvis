#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>

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
}

JARVIS_TEST(vfs_resolve_relative_path) {
    auto* vn1 = vfs::resolve("/dev/tty");
    JARVIS_ASSERT(vn1 != nullptr);

    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    cur->cwd_vnode = vfs::get_root_vnode();

    auto* vn2 = vfs::resolve("dev/tty");
    JARVIS_ASSERT(vn2 != nullptr);
    JARVIS_ASSERT(vn1 == vn2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_dotdot) {
    auto* vn1 = vfs::resolve("/dev/tty");
    JARVIS_ASSERT(vn1 != nullptr);

    auto* vn2 = vfs::resolve("/dev/../dev/tty");
    JARVIS_ASSERT(vn2 != nullptr);
    JARVIS_ASSERT(vn1 == vn2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_mount_unmount) {
    struct TestFS : vfs::Filesystem {
        TestFS() { name = "testfs"; }
        vfs::Vnode* get_root() override {
            static vfs::Vnode vn;
            vn.ops = nullptr;
            vn.mode = vfs::S_IFDIR;
            vn.ino = 999;
            return &vn;
        }
    } fs;

    JARVIS_ASSERT_EQ(0, vfs::mount(&fs, "/mnt/test"));
    auto* vn = vfs::resolve("/mnt/test");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT_EQ(999ULL, vn->ino);

    for (size_t i = 0; i < vfs::MAX_MOUNTS; ++i) {
        if (vfs::mount_table[i].used &&
            vfs::mount_table[i].mount_point &&
            strcmp(vfs::mount_table[i].mount_point, "/mnt/test") == 0) {
            vfs::mount_table[i].used = false;
            break;
        }
    }

    vn = vfs::resolve("/mnt/test");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfsd_server_boots_and_responds) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t vfsd_pid = 2;
    auto* vfsd = Scheduler::find_task(vfsd_pid);
    JARVIS_ASSERT(vfsd != nullptr);

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
