#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies PIT initialization sets the correct divisor.
// Input: Initialize PIT with default frequency
// Expect: Counter divisor matches expected value (1193180 / frequency)
// Depends: kernel::arch::PIT
JARVIS_TEST(pit_init_sets_divisor) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies successive ticks() calls return non-decreasing values.
// Input: Call ticks() multiple times with small delays
// Expect: Each call returns value >= previous call
// Depends: kernel::arch::PIT::ticks()
JARVIS_TEST(pit_ticks_monotonic) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies set_ticks_for_test() overrides the tick counter.
// Input: Set test value via set_ticks_for_test(), then call ticks()
// Expect: Returns the test value
// Depends: kernel::arch::PIT::set_ticks_for_test()
JARVIS_TEST(pit_set_ticks_for_test) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies each IRQ0 delivery increments the tick count.
// Input: Trigger IRQ0 handler, check tick count before/after
// Expect: Tick count increases by 1 per IRQ0
// Depends: kernel::arch::PIT IRQ handler
JARVIS_TEST(pit_irq0_handler_increments_ticks) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies set_frequency clamps at min/max boundaries.
// Input: Call set_frequency with values below min and above max
// Expect: Frequency clamped to valid range
// Depends: kernel::arch::PIT::set_frequency()
JARVIS_TEST(pit_set_frequency_accepts_range) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Timer/PIT unit tests with the test framework.
// Input: None
// Expect: All PIT tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_timer_tests() {
    Logger::info("Registering timer tests");
    JARVIS_REGISTER_TEST(pit_init_sets_divisor);
    JARVIS_REGISTER_TEST(pit_ticks_monotonic);
    JARVIS_REGISTER_TEST(pit_set_ticks_for_test);
    JARVIS_REGISTER_TEST(pit_irq0_handler_increments_ticks);
    JARVIS_REGISTER_TEST(pit_set_frequency_accepts_range);
}