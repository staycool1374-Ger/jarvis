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

// ---- Write tests (separate writable partition, not shared global) ----

static fat32::Fat32Partition* create_writable_partition() {
    extern uint8_t _binary_build_fat32_img_start[];
    extern uint8_t _binary_build_fat32_img_end[];
    uint64_t bytes = static_cast<uint64_t>(
        _binary_build_fat32_img_end - _binary_build_fat32_img_start);
    uint64_t sectors = bytes / fat32::SECTOR_SIZE;
    auto* dev = new kernel::block::MockBlockDevice(sectors);
    for (uint64_t i = 0; i < sectors; ++i) {
        uint8_t sector[fat32::SECTOR_SIZE];
        __builtin_memcpy(sector,
            _binary_build_fat32_img_start + i * fat32::SECTOR_SIZE,
            fat32::SECTOR_SIZE);
        dev->write_sector(i, sector);
    }
    return new fat32::Fat32Partition(*dev);
}

// Runmode: kernel
// Testidea: Verifies FAT32 mkdir via VnodeOps creates a directory entry.
// Input: ops->mkdir on root vnode with name "NEWDIR"
// Expect: Returns 0, lookup finds new directory
// Depends: kernel::vfs::VnodeOps::mkdir
JARVIS_TEST(vfs_fat32_mkdir) {
    auto* part = create_writable_partition();
    JARVIS_ASSERT(part->mount());
    auto* old = fat32_partition_instance;
    fat32_partition_instance = part;

    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->ops->mkdir != nullptr);

    int ret = root->ops->mkdir(*root, "NEWDIR", 0);
    JARVIS_ASSERT_EQ(0, ret);

    Vnode* child = root->ops->lookup(*root, "NEWDIR");
    JARVIS_ASSERT(child != nullptr);
    JARVIS_ASSERT(child->mode & vfs::S_IFDIR);

    child->ops->close(*child);
    root->ops->close(*root);
    fat32_partition_instance = old;
    fat32_fs.get_root();
    delete part;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies FAT32 unlink via VnodeOps removes a file entry.
// Input: ops->unlink on root vnode for "README.TXT"
// Expect: Returns 0, lookup no longer finds the file
// Depends: kernel::vfs::VnodeOps::unlink
JARVIS_TEST(vfs_fat32_unlink) {
    auto* part = create_writable_partition();
    JARVIS_ASSERT(part->mount());
    auto* old = fat32_partition_instance;
    fat32_partition_instance = part;

    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);
    JARVIS_ASSERT(root->ops->unlink != nullptr);

    int ret = root->ops->unlink(*root, "README.TXT");
    JARVIS_ASSERT_EQ(0, ret);

    Vnode* child = root->ops->lookup(*root, "README.TXT");
    JARVIS_ASSERT(child == nullptr);

    root->ops->close(*root);
    fat32_partition_instance = old;
    fat32_fs.get_root();
    delete part;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies mkdir then readdir shows the new directory.
// Input: mkdir "DIR2", then readdir
// Expect: readdir includes "DIR2"
// Depends: kernel::vfs::VnodeOps
JARVIS_TEST(vfs_fat32_mkdir_then_readdir) {
    auto* part = create_writable_partition();
    JARVIS_ASSERT(part->mount());
    auto* old = fat32_partition_instance;
    fat32_partition_instance = part;

    Vnode* root = fat32_fs.get_root();
    JARVIS_ASSERT(root != nullptr);

    JARVIS_ASSERT_EQ(0, root->ops->mkdir(*root, "DIR2", 0));

    uint64_t pos = 0;
    Dirent dent = {};
    bool found = false;
    while (root->ops->readdir(*root, pos, dent) == 0) {
        if (strcmp(dent.d_name, "DIR2") == 0) {
            found = true;
            break;
        }
    }
    JARVIS_ASSERT(found);

    root->ops->close(*root);
    fat32_partition_instance = old;
    fat32_fs.get_root();
    delete part;
    JARVIS_TEST_PASS();
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

    JARVIS_REGISTER_TEST(vfs_fat32_mkdir);
    JARVIS_REGISTER_TEST(vfs_fat32_unlink);
    JARVIS_REGISTER_TEST(vfs_fat32_mkdir_then_readdir);
}
