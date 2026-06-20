#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>

using namespace kernel;
using namespace kernel::vfs;

// Runmode: kernel
// Testidea: Resolving "/dev/random" returns a character-device vnode
// Input: vfs::resolve("/dev/random")
// Expect: Non-null vnode with S_IFCHR mode set
// Depends: kernel::vfs::resolve
JARVIS_TEST(dev_random_resolve) {
    Vnode* vn = vfs::resolve("/dev/random");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & S_IFCHR);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Reading from /dev/random via vnode ops returns non-zero bytes
// Input: vnode->ops->read on /dev/random vnode, 256-byte buffer
// Expect: Returns 256 bytes, buffer not all-zero or all-FF
// Depends: kernel::vfs::resolve, VnodeOps::read
JARVIS_TEST(dev_random_read) {
    Vnode* vn = vfs::resolve("/dev/random");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->ops != nullptr);
    JARVIS_ASSERT(vn->ops->read != nullptr);

    uint8_t buf[256];
    int64_t nread = vn->ops->read(*vn, buf, sizeof(buf), 0);
    JARVIS_ASSERT_EQ(static_cast<int64_t>(sizeof(buf)), nread);

    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0)  all_zero = false;
        if (buf[i] != 0xFF) all_ff = false;
    }
    JARVIS_ASSERT_FMT(!all_zero, "read /dev/random returned %zu zero bytes", sizeof(buf));
    JARVIS_ASSERT_FMT(!all_ff, "read /dev/random returned %zu 0xFF bytes", sizeof(buf));

    JARVIS_TEST_PASS();
}

void register_random_vfs_tests() {
    Logger::info("Registering /dev/random VFS tests");
    JARVIS_REGISTER_TEST(dev_random_resolve);
    JARVIS_REGISTER_TEST(dev_random_read);
}
