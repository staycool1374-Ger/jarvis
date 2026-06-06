/// @file test.hpp
/// @brief Unified macro-driven kernel test framework.

#pragma once

#include <types.hpp>

namespace kernel {
namespace test {

struct TestCase {
    const char* suite;
    const char* name;
    void (*func)();
};

class Registry {
public:
    static void init();
    static void register_test(const TestCase& tc);
    static const TestCase* tests();
    static size_t count();

    static void record_failure(const char* file, int line, const char* expr);
    static void record_success();

    static size_t passed();
    static size_t failed();
    static size_t total();
    static void reset();

private:
    static constexpr size_t MAX_TESTS = 256;
    static TestCase tests_[MAX_TESTS];
    static size_t count_;
    static size_t passed_;
    static size_t failed_;
};

void run_all();
void run_suite(const char* suite_name);
void print_report();

} // namespace test
} // namespace kernel

#define JARVIS_TEST(name)                                                     \
    static void test_##name();                                                 \
    static void test_##name()

#define JARVIS_TEST_SUITE(suite, name)                                        \
    static void test_##suite##_##name();                                       \
    static void test_##suite##_##name()

#define JARVIS_ASSERT(cond)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            kernel::test::Registry::record_failure(                            \
                __FILE__, __LINE__, #cond);                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define JARVIS_ASSERT_HEX_EQ(expected, actual)                                \
    do {                                                                       \
        uint64_t _exp = static_cast<uint64_t>(expected);                       \
        uint64_t _act = static_cast<uint64_t>(actual);                         \
        if (_exp != _act) {                                                    \
            kernel::test::Registry::record_failure(                            \
                __FILE__, __LINE__,                                            \
                #actual " == " #expected " (exp=0x" #expected ")");            \
            return;                                                            \
        }                                                                      \
        kernel::test::Registry::record_success();                              \
    } while (0)

#define JARVIS_ASSERT_EQ(expected, actual)                                    \
    do {                                                                       \
        if ((expected) != (actual)) {                                          \
            kernel::test::Registry::record_failure(                            \
                __FILE__, __LINE__,                                            \
                #actual " != " #expected);                                     \
            return;                                                            \
        }                                                                      \
        kernel::test::Registry::record_success();                              \
    } while (0)

#define JARVIS_TEST_PASS()                                                     \
    kernel::test::Registry::record_success()

#define JARVIS_REGISTER_TEST(name)                                            \
    do {                                                                       \
        static constexpr kernel::test::TestCase tc = {                         \
            "", #name, test_##name                                             \
        };                                                                     \
        kernel::test::Registry::register_test(tc);                             \
    } while (0)

#define JARVIS_REGISTER_TEST_SUITE(suite, name)                                \
    do {                                                                       \
        static constexpr kernel::test::TestCase tc = {                         \
            #suite, #name, test_##suite##_##name                               \
        };                                                                     \
        kernel::test::Registry::register_test(tc);                             \
    } while (0)
