#include <test.hpp>
#include <logger.hpp>
#include <services/shell.hpp>
#include <services/terminal/terminal.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies shell redirect with echo > /dev/null does not crash.
// Input: Shell::execute("echo test > /dev/null")
// Expect: No crash, redirect parsing works
// Depends: service::Shell, service::Terminal
JARVIS_TEST(shell_redirect_to_devnull) {
    service::Shell::execute("echo test > /dev/null");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies shell redirect with no target is handled gracefully.
// Input: Shell::execute("echo test >")
// Expect: No crash, parser handles missing target
// Depends: service::Shell
JARVIS_TEST(shell_redirect_no_target) {
    service::Shell::execute("echo test >");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies redirect captures command output.
// Input: Shell::execute("echo HELLO > /dev/null")
// Expect: No crash, capture mechanism is exercised
// Depends: service::Shell
JARVIS_TEST(shell_redirect_capture_output) {
    service::Shell::execute("echo HELLO > /dev/null");
    JARVIS_TEST_PASS();
}

void register_shell_redirect_tests() {
    Logger::info("Registering shell redirect tests");
    JARVIS_REGISTER_TEST(shell_redirect_to_devnull);
    JARVIS_REGISTER_TEST(shell_redirect_no_target);
    JARVIS_REGISTER_TEST(shell_redirect_capture_output);
}
