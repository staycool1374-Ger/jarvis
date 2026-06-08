#include <test.hpp>
#include <logger.hpp>
#include <signal.hpp>
#include <kernel/memory/checked_ptr.hpp>

using namespace kernel;

JARVIS_TEST(checked_ptr_is_user_range) {
    JARVIS_ASSERT(!is_user_range(reinterpret_cast<void*>(0xFFFF800000000000ULL), 1));

    JARVIS_ASSERT(is_user_range(reinterpret_cast<void*>(0x400000), 1));

    JARVIS_ASSERT(!is_user_range(nullptr, 1));

    uint64_t limit = USER_SPACE_LIMIT;
    JARVIS_ASSERT(!is_user_range(reinterpret_cast<void*>(limit), 1));
    JARVIS_ASSERT(is_user_range(reinterpret_cast<void*>(limit - 1), 1));

    JARVIS_ASSERT(!is_user_range(reinterpret_cast<void*>(limit - 10), 20));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(checked_ptr_valid) {
    char buf[64] = "test data";
    CheckedPtr<char> cp(buf, sizeof(buf));
    JARVIS_ASSERT(!cp.valid());

    CheckedPtr<char> null_cp(nullptr);
    JARVIS_ASSERT(!null_cp.valid());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(checked_ptr_is_user_string) {
    JARVIS_ASSERT(!is_user_string("kernel string"));

    JARVIS_ASSERT(!is_user_string(nullptr));

    char user_buf[] = "hello";
    JARVIS_ASSERT(!is_user_string(user_buf));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_frame_size) {
    JARVIS_ASSERT_EQ(64ULL, sizeof(SignalFrame));
    JARVIS_ASSERT_EQ(static_cast<size_t>(64), SIGNAL_FRAME_SIZE);

    SignalFrame sf{};
    sf.sig = 11;
    sf.saved_rip = 0x400000;
    sf.saved_rsp = 0x70000000;
    sf.saved_rflags = 0x202;
    sf.saved_cs = 0x1B;
    sf.saved_ss = 0x23;
    JARVIS_ASSERT_EQ(11ULL, sf.sig);
    JARVIS_ASSERT_EQ(0x400000ULL, sf.saved_rip);
    JARVIS_ASSERT_EQ(0x70000000ULL, sf.saved_rsp);
    JARVIS_ASSERT_EQ(0x202ULL, sf.saved_rflags);
    JARVIS_ASSERT_EQ(0x1BULL, sf.saved_cs);
    JARVIS_ASSERT_EQ(0x23ULL, sf.saved_ss);
    JARVIS_TEST_PASS();
}

void register_checked_ptr_tests() {
    Logger::info("Registering CheckedPtr tests");

    JARVIS_REGISTER_TEST(checked_ptr_is_user_range);
    JARVIS_REGISTER_TEST(checked_ptr_valid);
    JARVIS_REGISTER_TEST(checked_ptr_is_user_string);
    JARVIS_REGISTER_TEST(signal_frame_size);
}
