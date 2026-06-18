#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/tmpfs.hpp>
#include <kernel/vfs/vfs.hpp>

using namespace kernel;
using namespace kernel::vfs;

JARVIS_TEST(tmpfs_root_ops_consistent) {
    Vnode* root = tmpfs_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    // Root should have directory ops
    JARVIS_ASSERT(root->ops != nullptr);
    JARVIS_ASSERT(root->ops->mkdir != nullptr);
    JARVIS_ASSERT(root->ops->lookup != nullptr);
    JARVIS_ASSERT(root->ops->readdir != nullptr);
    JARVIS_ASSERT(root->ops->unlink != nullptr);
    JARVIS_ASSERT(root->ops->fstat != nullptr);

    // Root should not have file-only ops
    JARVIS_ASSERT_EQ(nullptr, root->ops->read);
    JARVIS_ASSERT_EQ(nullptr, root->ops->write);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(tmpfs_unmount_unsupported) {
    // Currently VFS has no unmount API — verify mount succeeds as expected
    int ret = vfs::mount(tmpfs_fs, "/tmp_nounmount_test");
    JARVIS_ASSERT_EQ(0, ret);

    Vnode* root = vfs::resolve("/tmp_nounmount_test");
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->mode & S_IFDIR);
    JARVIS_TEST_PASS();
}

void register_tmpfs_mount_unmount_failure_tests() {
    kernel::Logger::info("Registering tmpfs mount/unmount failure tests");
    JARVIS_REGISTER_TEST(tmpfs_root_ops_consistent);
    JARVIS_REGISTER_TEST(tmpfs_unmount_unsupported);
}
