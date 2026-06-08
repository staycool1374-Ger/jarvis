#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/rtc.hpp>

using namespace kernel;

JARVIS_TEST(rtc_read_seconds) {
    uint64_t secs = arch::RTC::read_seconds();
    JARVIS_ASSERT(secs > 1577836800ULL);
    JARVIS_ASSERT(secs < 7258118400ULL);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(rtc_bcd_conversion) {
    JARVIS_ASSERT_EQ(0x00, arch::RTC::bcd_to_bin(0x00));
    JARVIS_ASSERT_EQ(0x09, arch::RTC::bcd_to_bin(0x09));
    JARVIS_ASSERT_EQ(0x0A, arch::RTC::bcd_to_bin(0x10));
    JARVIS_ASSERT_EQ(0x0F, arch::RTC::bcd_to_bin(0x15));
    JARVIS_ASSERT_EQ(0x17, arch::RTC::bcd_to_bin(0x23));
    JARVIS_ASSERT_EQ(0x3B, arch::RTC::bcd_to_bin(0x59));
    JARVIS_TEST_PASS();
}

void register_rtc_tests() {
    Logger::info("Registering RTC tests");
    JARVIS_REGISTER_TEST(rtc_read_seconds);
    JARVIS_REGISTER_TEST(rtc_bcd_conversion);
}
