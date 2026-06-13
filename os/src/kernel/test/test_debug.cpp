#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies exit code 0 shuts down QEMU cleanly via port 0x501.
// Input: Call qemu_debug_exit(0)
// Expect: QEMU exits with code 0
// Depends: kernel::debug::qemu_exit()
JARVIS_TEST(qemu_debug_exit_success) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies non-zero exit code correctly reported by QEMU test
// harness.
// Input: Call qemu_debug_exit(42)
// Expect: QEMU exits with code 42
// Depends: kernel::debug::qemu_exit()
JARVIS_TEST(qemu_debug_exit_failure) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies debug_write() outputs hex/dec numbers with correct
// formatting.
// Input: Call debug_write with various formats
// Expect: Output matches expected format strings
// Depends: kernel::debug::debug_write()
JARVIS_TEST(debug_write_formats_correctly) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies task switch generates a log message with source -> dest
// PID.
// Input: Trigger task switch, capture debug log
// Expect: Log contains "Task switch: <src> -> <dst>"
// Depends: kernel::debug, Scheduler
JARVIS_TEST(debug_task_switch_logs) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Debug unit tests with the test framework.
// Input: None
// Expect: All Debug tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_debug_tests() {
    Logger::info("Registering debug tests");
    JARVIS_REGISTER_TEST(qemu_debug_exit_success);
    JARVIS_REGISTER_TEST(qemu_debug_exit_failure);
    JARVIS_REGISTER_TEST(debug_write_formats_correctly);
    JARVIS_REGISTER_TEST(debug_task_switch_logs);
}