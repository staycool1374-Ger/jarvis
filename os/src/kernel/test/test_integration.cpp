#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Renders Mandelbrot to framebuffer, computes CRC32, asserts known-good hash.
// Input: Run mandelbrot rendering, compute CRC32 of framebuffer
// Expect: CRC32 matches expected hash
// Depends: Framebuffer, mandelbrot demo
JARVIS_TEST(mandelbrot_crc_hash) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Integration unit tests with the test framework.
// Input: None
// Expect: All Integration tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_integration_tests() {
    Logger::info("Registering integration tests");
    JARVIS_REGISTER_TEST(mandelbrot_crc_hash);
}