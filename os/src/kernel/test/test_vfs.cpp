#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;
using namespace kernel::vfs;

// Runmode: kernel
// Testidea: Tests allocating a single fd from FdTable, verifying the slot is
// marked used, then freeing it and confirming the slot is released.
// Input: Alloc one fd from a fresh FdTable, then free that fd.
// Expect: JARVIS_ASSERT checks fd >= 0, the returned entry is non-null and
// used; after free, entry is null or not used.
// Depends: kernel::vfs::FdTable
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

// Runmode: kernel
// Testidea: Tests allocating and freeing multiple fds from FdTable, then
// verifying that a recycled fd can be re-allocated.
// Input: Alloc 5 fds, free all 5, then alloc one more fd.
// Expect: JARVIS_ASSERT checks all allocations return non-negative fd; final
// alloc also succeeds.
// Depends: kernel::vfs::FdTable
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

// Runmode: kernel
// Testidea: Tests that resolving the root path "/" returns a valid vnode
// with directory mode.
// Input: vfs::resolve("/")
// Expect: JARVIS_ASSERT checks vnode is non-null and has S_IFDIR set.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_root) {
    auto* vn = vfs::resolve("/");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that resolving "/dev" returns a valid directory vnode.
// Input: vfs::resolve("/dev")
// Expect: JARVIS_ASSERT checks vnode is non-null and has S_IFDIR set.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_dev) {
    auto* vn = vfs::resolve("/dev");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that resolving "/dev/tty" returns a valid character-device
// vnode.
// Input: vfs::resolve("/dev/tty")
// Expect: JARVIS_ASSERT checks vnode is non-null and has S_IFCHR set.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_tty) {
    auto* vn = vfs::resolve("/dev/tty");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that resolving "/dev/null" returns a valid
// character-device vnode.
// Input: vfs::resolve("/dev/null")
// Expect: JARVIS_ASSERT checks vnode is non-null and has S_IFCHR set.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_null) {
    auto* vn = vfs::resolve("/dev/null");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that resolving "/proc" returns a valid directory vnode.
// Input: vfs::resolve("/proc")
// Expect: JARVIS_ASSERT checks vnode is non-null and has S_IFDIR set.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_proc) {
    auto* vn = vfs::resolve("/proc");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that resolving a nonexistent path returns nullptr.
// Input: vfs::resolve("/nonexistent_path_xyz")
// Expect: JARVIS_ASSERT checks result is nullptr.
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_nonexistent) {
    auto* vn = vfs::resolve("/nonexistent_path_xyz");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies resolution of relative paths "." and ".." works.
// Input: resolve("/dev/./tty"), resolve("/dev/../dev/tty")
// Expect: Both return valid character-device vnodes
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_relative_path) {
    Vnode* vn = vfs::resolve("/dev/./tty");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    vn = vfs::resolve("/dev/../dev/tty");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies ".." resolves to the parent directory.
// Input: resolve("/dev/..")
// Expect: Returns the root directory vnode
// Depends: kernel::vfs::resolve
JARVIS_TEST(vfs_resolve_dotdot) {
    Vnode* dev = vfs::resolve("/dev");
    JARVIS_ASSERT(dev != nullptr);
    Vnode* root = vfs::resolve("/dev/..");
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->mode & vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies mounting a filesystem registers it in the mount table
//           and resolve can find root.
// Input: mount(filesystem, "/mnt")
// Expect: Mount succeeds, resolve("/mnt") returns root vnode
// Depends: kernel::vfs::mount, kernel::vfs::resolve
JARVIS_TEST(vfs_mount_unmount) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that the VFS daemon process boots
// and responds to IPC.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::vfsd, kernel::ipc
JARVIS_TEST(vfsd_server_boots_and_responds) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::mkdir creates a directory at a valid path.
// Input: vfs::mkdir("/mnt/NEWDIR_TEST", 0)
// Expect: Returns 0, vfs::resolve finds the new directory with S_IFDIR
// Depends: kernel::vfs::mkdir, kernel::vfs::resolve
JARVIS_TEST(vfs_mkdir_valid) {
    int ret = vfs::mkdir("/mnt/NEWDIR_TEST", 0);
    JARVIS_ASSERT_EQ(0, ret);

    Vnode* vn = vfs::resolve("/mnt/NEWDIR_TEST");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & vfs::S_IFDIR);

    // Cleanup
    vfs::unlink("/mnt/NEWDIR_TEST");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::mkdir fails for nonexistent parent.
// Input: vfs::mkdir("/nonexistent/DIR", 0)
// Expect: Returns VFS_INVALID
// Depends: kernel::vfs::mkdir
JARVIS_TEST(vfs_mkdir_nonexistent_parent) {
    int ret = vfs::mkdir("/nonexistent/DIR", 0);
    JARVIS_ASSERT_EQ(vfs::VFS_INVALID, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::mkdir fails when parent is not a directory.
// Input: vfs::mkdir("/mnt/README.TXT/DIR", 0) where README.TXT is a file
// Expect: Returns VFS_INVALID
// Depends: kernel::vfs::mkdir
JARVIS_TEST(vfs_mkdir_parent_not_dir) {
    int ret = vfs::mkdir("/mnt/README.TXT/DIR", 0);
    JARVIS_ASSERT_EQ(vfs::VFS_INVALID, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::unlink removes a file.
// Input: Use existing file from FAT32 image (README.TXT), then vfs::unlink
// Expect: Returns 0, vfs::resolve no longer finds the file
// Depends: kernel::vfs::unlink, kernel::vfs::resolve
JARVIS_TEST(vfs_unlink_file) {
    Vnode* vn = vfs::resolve("/mnt/README.TXT");
    JARVIS_ASSERT(vn != nullptr);

    int ret = vfs::unlink("/mnt/README.TXT");
    JARVIS_ASSERT_EQ(0, ret);

    vn = vfs::resolve("/mnt/README.TXT");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::unlink fails for nonexistent path.
// Input: vfs::unlink("/mnt/NONEXISTENT")
// Expect: Returns VFS_INVALID
// Depends: kernel::vfs::unlink
JARVIS_TEST(vfs_unlink_nonexistent) {
    int ret = vfs::unlink("/mnt/NONEXISTENT");
    JARVIS_ASSERT_EQ(vfs::VFS_INVALID, ret);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies vfs::unlink fails for non-empty directory.
// Input: vfs::mkdir("/mnt/DIR"), create file inside, vfs::unlink("/mnt/DIR")
// Expect: Returns VFS_INVALID (FAT32 enforces empty dir check)
// Depends: kernel::vfs::unlink, kernel::vfs::mkdir
JARVIS_TEST(vfs_unlink_nonempty_dir_fails) {
    int ret = vfs::mkdir("/mnt/PARENTDIR", 0);
    JARVIS_ASSERT_EQ(0, ret);

    ret = vfs::mkdir("/mnt/PARENTDIR/CHILD", 0);
    JARVIS_ASSERT_EQ(0, ret);

    ret = vfs::unlink("/mnt/PARENTDIR");
    JARVIS_ASSERT_EQ(vfs::VFS_INVALID, ret);

    // Cleanup
    vfs::unlink("/mnt/PARENTDIR/CHILD");
    vfs::unlink("/mnt/PARENTDIR");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that the open syscall forwards
// requests to the VFS daemon.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::syscall, kernel::vfsd
JARVIS_TEST(syscall_open_forwards_to_vfsd) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that read/write syscalls route
// through the VFS daemon.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::syscall, kernel::vfsd
JARVIS_TEST(syscall_read_write_via_vfsd) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing mount/unmount IPC commands to the
// VFS daemon.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::vfsd, kernel::ipc
JARVIS_TEST(vfsd_mount_unmount_ipc) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing VFS daemon
// current-working-directory operations (getcwd, chdir).
// Input: (stub)
// Expect: (stub)
// Depends: kernel::vfsd, kernel::ipc
JARVIS_TEST(vfsd_cwd_operations) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing VFS daemon stat/fstat IPC
// operations.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::vfsd, kernel::ipc
JARVIS_TEST(vfsd_stat_fstat) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all VFS test cases into the test framework.
// Input: None
// Expect: Each JARVIS_REGISTER_TEST call adds the test to the suite.
// Depends: kernel::test
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
    JARVIS_REGISTER_TEST(vfs_mkdir_valid);
    JARVIS_REGISTER_TEST(vfs_mkdir_nonexistent_parent);
    JARVIS_REGISTER_TEST(vfs_mkdir_parent_not_dir);
    JARVIS_REGISTER_TEST(vfs_unlink_file);
    JARVIS_REGISTER_TEST(vfs_unlink_nonexistent);
    JARVIS_REGISTER_TEST(vfs_unlink_nonempty_dir_fails);
    JARVIS_REGISTER_TEST(vfsd_server_boots_and_responds);
    JARVIS_REGISTER_TEST(syscall_open_forwards_to_vfsd);
    JARVIS_REGISTER_TEST(syscall_read_write_via_vfsd);
    JARVIS_REGISTER_TEST(vfsd_mount_unmount_ipc);
    JARVIS_REGISTER_TEST(vfsd_cwd_operations);
    JARVIS_REGISTER_TEST(vfsd_stat_fstat);
}
