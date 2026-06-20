#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>

using namespace kernel;
using namespace kernel::vfs;

// Runmode: kernel
// Testidea: Writing to /dev/random via vnode ops returns the byte count
// Input: vnode->ops->write on /dev/random vnode with 128-byte buffer
// Expect: Returns 128 (write is a no-op that echoes count)
// Depends: kernel::vfs::resolve, VnodeOps::write
JARVIS_TEST(dev_random_write) {
    Vnode* vn = vfs::resolve("/dev/random");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->ops != nullptr);
    JARVIS_ASSERT(vn->ops->write != nullptr);

    uint8_t buf[128] = {42};
    int64_t written = vn->ops->write(*vn, buf, sizeof(buf), 0);
    JARVIS_ASSERT_EQ(static_cast<int64_t>(sizeof(buf)), written);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Writing zero bytes to /dev/random returns zero
// Input: vnode->ops->write on /dev/random with count=0
// Expect: Returns 0
JARVIS_TEST(dev_random_write_zero) {
    Vnode* vn = vfs::resolve("/dev/random");
    JARVIS_ASSERT(vn != nullptr);

    int64_t written = vn->ops->write(*vn, nullptr, 0, 0);
    JARVIS_ASSERT_EQ(0LL, written);

    JARVIS_TEST_PASS();
}

void register_random_vfs_write_tests() {
    Logger::info("Registering /dev/random VFS write tests");
    JARVIS_REGISTER_TEST(dev_random_write);
    JARVIS_REGISTER_TEST(dev_random_write_zero);
}
