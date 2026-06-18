#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/tmpfs.hpp>
#include <kernel/vfs/vfs.hpp>
#include <string.hpp>

using namespace kernel;
using namespace kernel::vfs;

JARVIS_TEST(tmpfs_filesystem_properties) {
    JARVIS_ASSERT(tmpfs_fs.get_root != nullptr);
    JARVIS_ASSERT(tmpfs_fs.name != nullptr);
    JARVIS_ASSERT_EQ(0, strcmp(tmpfs_fs.name, "tmpfs"));

    Vnode* root = tmpfs_fs.get_root();
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->mode & S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(tmpfs_mount_at_invalid_resolve_fails) {
    Vnode* vn = vfs::resolve("/nonexistent_tmpfs_mount");
    JARVIS_ASSERT_EQ(nullptr, vn);
    JARVIS_TEST_PASS();
}

void register_tmpfs_invalid_mount_tests() {
    kernel::Logger::info("Registering tmpfs invalid mount tests");
    JARVIS_REGISTER_TEST(tmpfs_filesystem_properties);
    JARVIS_REGISTER_TEST(tmpfs_mount_at_invalid_resolve_fails);
}
