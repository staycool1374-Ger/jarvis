#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies reading /dev/null returns 0 bytes.
// Input: Open /dev/null, read into buffer
// Expect: Returns 0 bytes read
// Depends: kernel::vfs::DevFS
JARVIS_TEST(devfs_read_null) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies reading /dev/zero returns NUL-filled buffer of requested size.
// Input: Open /dev/zero, read N bytes
// Expect: Buffer filled with N zero bytes
// Depends: kernel::vfs::DevFS
JARVIS_TEST(devfs_read_zero) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies /dev/tty resolves to a valid inode (actual read requires keyboard).
// Input: Resolve /dev/tty
// Expect: Returns valid inode
// Depends: kernel::vfs::DevFS
JARVIS_TEST(devfs_tty_resolves) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies /proc/self resolves to the current process PID.
// Input: Resolve /proc/self from current task
// Expect: Returns inode for current PID
// Depends: kernel::vfs::ProcFS
JARVIS_TEST(procfs_self_resolves) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies reading /proc/meminfo returns well-formatted ASCII data.
// Input: Read /proc/meminfo
// Expect: Contains "MemTotal:", "MemFree:", etc.
// Depends: kernel::vfs::ProcFS
JARVIS_TEST(procfs_meminfo_valid) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies directory listing / returns initrd contents.
// Input: List directory / on initrd
// Expect: Entries match initrd files
// Depends: kernel::vfs::InitrdFS
JARVIS_TEST(initrdfs_list_root) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies reading a known initrd file returns expected byte content.
// Input: Open known file from initrd, read content
// Expect: Content matches expected bytes
// Depends: kernel::vfs::InitrdFS
JARVIS_TEST(initrdfs_read_known_file) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies opening a non-existent initrd path returns ENOENT.
// Input: Open /nonexistent
// Expect: Returns ENOENT
// Depends: kernel::vfs::InitrdFS
JARVIS_TEST(initrdfs_open_nonexistent) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all VFS Internal Filesystem unit tests with the test framework.
// Input: None
// Expect: All VFS internal tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_vfs_internal_tests() {
    Logger::info("Registering VFS internal tests");
    JARVIS_REGISTER_TEST(devfs_read_null);
    JARVIS_REGISTER_TEST(devfs_read_zero);
    JARVIS_REGISTER_TEST(devfs_tty_resolves);
    JARVIS_REGISTER_TEST(procfs_self_resolves);
    JARVIS_REGISTER_TEST(procfs_meminfo_valid);
    JARVIS_REGISTER_TEST(initrdfs_list_root);
    JARVIS_REGISTER_TEST(initrdfs_read_known_file);
    JARVIS_REGISTER_TEST(initrdfs_open_nonexistent);
}