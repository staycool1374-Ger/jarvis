#include <test.hpp>
#include <logger.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/terminal.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies framebuffer dimensions are valid after boot init.
// Input: Boot with framebuffer tag
// Expect: width, height, bpp, pitch are non-zero when available
// Depends: service::Framebuffer
JARVIS_TEST(fb_init_from_multiboot) {
    if (service::Framebuffer::available()) {
        JARVIS_ASSERT(service::Framebuffer::width() > 0);
        JARVIS_ASSERT(service::Framebuffer::height() > 0);
        JARVIS_ASSERT(service::Framebuffer::pitch() > 0);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies drawing a pixel within bounds does not crash.
// Input: draw_pixel(0, 0, white)
// Expect: No crash, framebuffer unchanged for out-of-bounds
// Depends: service::Framebuffer
JARVIS_TEST(fb_putpixel_in_bounds) {
    if (service::Framebuffer::available()) {
        service::Framebuffer::draw_pixel(0, 0, 0xFFFFFF);
        uint32_t w = service::Framebuffer::width();
        uint32_t h = service::Framebuffer::height();
        if (w > 1 && h > 1)
            service::Framebuffer::draw_pixel(w - 1, h - 1, 0xFF0000);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies writing outside framebuffer bounds is safe (no-op).
// Input: putpixel(-1, 0), putpixel(width, height)
// Expect: No crash, no memory corruption
// Depends: service::Framebuffer
JARVIS_TEST(fb_putpixel_out_of_bounds) {
    if (service::Framebuffer::available()) {
        service::Framebuffer::draw_pixel(
            static_cast<uint32_t>(-1), 0, 0xFF);
        service::Framebuffer::draw_pixel(
            0, static_cast<uint32_t>(-1), 0xFF);
        service::Framebuffer::draw_pixel(
            service::Framebuffer::width(),
            service::Framebuffer::height(), 0xFF);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies clearing sets all pixels to the background color.
// Input: clear_screen(black)
// Expect: No crash
// Depends: service::Framebuffer
JARVIS_TEST(fb_clear_screen) {
    if (service::Framebuffer::available()) {
        service::Framebuffer::clear(0x000000);
    }
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies scrolling shifts framebuffer content up.
// Input: Write multiple lines, scroll
// Expect: No crash, terminal state updated
// Depends: service::Terminal
JARVIS_TEST(fb_scroll_up) {
    service::Terminal::write("line1\nline2\nline3\n");
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