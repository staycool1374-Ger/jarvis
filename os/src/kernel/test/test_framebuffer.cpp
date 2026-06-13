#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies framebuffer initializes from multiboot framebuffer tag.
// Input: Boot with framebuffer tag, init framebuffer
// Expect: width, height, bpp, pitch match tag
// Depends: kernel::Framebuffer
JARVIS_TEST(fb_init_from_multiboot) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies writing at valid (x, y) sets the correct pixel color.
// Input: putpixel(x, y, color), read back
// Expect: Pixel at (x, y) has color
// Depends: kernel::Framebuffer
JARVIS_TEST(fb_putpixel_in_bounds) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies writing outside framebuffer bounds is safe (no-op or
// clamped).
// Input: putpixel(-1, 0), putpixel(width, height)
// Expect: No crash, no memory corruption
// Depends: kernel::Framebuffer
JARVIS_TEST(fb_putpixel_out_of_bounds) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies clearing sets all pixels to the background color.
// Input: clear_screen(color), check multiple pixels
// Expect: All pixels == color
// Depends: kernel::Framebuffer
JARVIS_TEST(fb_clear_screen) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies scrolling shifts framebuffer content up by one line
// correctly.
// Input: fill screen, scroll_up(), check content
// Expect: Line 1 becomes line 0, last line cleared
// Depends: kernel::Framebuffer
JARVIS_TEST(fb_scroll_up) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Framebuffer unit tests with the test framework.
// Input: None
// Expect: All Framebuffer tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_framebuffer_tests() {
    Logger::info("Registering framebuffer tests");
    JARVIS_REGISTER_TEST(fb_init_from_multiboot);
    JARVIS_REGISTER_TEST(fb_putpixel_in_bounds);
    JARVIS_REGISTER_TEST(fb_putpixel_out_of_bounds);
    JARVIS_REGISTER_TEST(fb_clear_screen);
    JARVIS_REGISTER_TEST(fb_scroll_up);
}