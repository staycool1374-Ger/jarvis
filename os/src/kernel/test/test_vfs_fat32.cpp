#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/fat32.hpp>
#include <kernel/vfs/fat32_fs.hpp>
#include <kernel/driver/block_device.hpp>
#include <kernel/memory/mempool.hpp>
#include <string.hpp>

using namespace kernel;
using namespace kernel::vfs;

extern "C" uint8_t _binary_build_fat32_img_start[];
extern "C" uint8_t _binary_build_fat32_img_end[];

static void setup_fat32_fs() {
    if (fat32_partition_instance) return;

    uint64_t bytes = static_cast<uint64_t>(
        _binary_build_fat32_img_end - _binary_build_fat32_img_start);

    // Allocate a writable partition that wraps the embedded image
    auto* dev = new kernel::block::MockBlockDevice(
        _binary_build_fat32_img_start, bytes / fat32::SECTOR_SIZE, true);
    auto* partition = new fat32::Fat32Partition(*dev);
    if (partition && partition->mount()) {
        fat32_partition_instance = partition;
    }
}

JARVIS_TEST(vfs_fat32_mount) {
    setup_fat32_fs();
    JARVIS_ASSERT(fat32_partition_instance != nullptr);
    JARVIS_ASSERT(fat32_partition_instance->bpb().valid);
}

JARVIS_TEST(vfs_fat32_open_root) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->mode == S_IFDIR);
    JARVIS_ASSERT(root->ops != nullptr);
}

JARVIS_TEST(vfs_fat32_open_file) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    Vnode* file = root->ops->lookup(*root, "README.TXT");
    JARVIS_ASSERT(file != nullptr);
    JARVIS_ASSERT(file->mode == S_IFREG);
    file->ops->close(*file);
}

JARVIS_TEST(vfs_fat32_read_file) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    Vnode* file = root->ops->lookup(*root, "HELLO.TXT");
    JARVIS_ASSERT(file != nullptr);

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    int64_t nread = file->ops->read(*file, buf, sizeof(buf), 0);
    JARVIS_ASSERT(nread > 0);
    JARVIS_ASSERT(memcmp(buf, "Hello, World!\n", 14) == 0);
    file->ops->close(*file);
}

JARVIS_TEST(vfs_fat32_fstat) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    Vnode* file = root->ops->lookup(*root, "README.TXT");
    JARVIS_ASSERT(file != nullptr);

    VfsStat st = {};
    JARVIS_ASSERT(file->ops->fstat(*file, st) == 0);
    JARVIS_ASSERT(st.st_size > 0);
    JARVIS_ASSERT(st.st_mode == S_IFREG);
    file->ops->close(*file);
}

JARVIS_TEST(vfs_fat32_readdir) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    uint64_t pos = 0;
    Dirent dent = {};
    bool found_readme = false;
    bool found_hello = false;

    while (root->ops->readdir(*root, pos, dent) == 0) {
        if (strcmp(dent.d_name, "README.TXT") == 0) found_readme = true;
        if (strcmp(dent.d_name, "HELLO.TXT") == 0) found_hello = true;
    }

    JARVIS_ASSERT(found_readme);
    JARVIS_ASSERT(found_hello);
}

JARVIS_TEST(vfs_fat32_nonexistent_path) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    Vnode* none = root->ops->lookup(*root, "NONEXIST.TXT");
    JARVIS_ASSERT(none == nullptr);
}

JARVIS_TEST(vfs_fat32_subdir) {
    setup_fat32_fs();
    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    Vnode* sub = root->ops->lookup(*root, "SUBDIR");
    JARVIS_ASSERT(sub != nullptr);
    JARVIS_ASSERT(sub->mode == S_IFDIR);

    Vnode* file = sub->ops->lookup(*sub, "FILE.TXT");
    JARVIS_ASSERT(file != nullptr);
    JARVIS_ASSERT(file->mode == S_IFREG);

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    int64_t nread = file->ops->read(*file, buf, sizeof(buf), 0);
    JARVIS_ASSERT(nread > 0);
    JARVIS_ASSERT(memcmp(buf, "I am in a subdirectory.\n", 24) == 0);

    file->ops->close(*file);
    sub->ops->close(*sub);
}

void register_vfs_fat32_tests() {
    Logger::info("Registering VFS FAT32 tests");

    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_mount);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_open_root);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_open_file);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_read_file);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_fstat);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_readdir);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_nonexistent_path);
    JARVIS_REGISTER_RELEASE_TEST(vfs_fat32_subdir);
}
