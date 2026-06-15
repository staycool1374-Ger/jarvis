#pragma once

#include <types.hpp>
#include <logger.hpp>

namespace kernel {
namespace test {

enum TestFlags : uint8_t {
    TF_KERNEL   = 0,
    TF_RELEASE  = 1 << 0,
    TF_USER     = 1 << 1,
};

class TestBase;

struct TestCase {
    const char* suite;
    const char* name;
    void (*func)();
    TestBase* (*factory)();
    uint8_t flags;
};

class Registry {
public:
    static void init();
    static void register_test(const TestCase& tc);
    static const TestCase* tests();
    static size_t count();

    static void record_failure(const char* file, int line, const char* expr);
    static void record_success();
    static void record_test(bool passed);

    static size_t passed();
    static size_t failed();
    static size_t total();
    static size_t test_count();
    static size_t test_passed();
    static size_t test_failed();
    static void reset();

private:
    static constexpr size_t MAX_TESTS = 1024;
    static TestCase tests_[MAX_TESTS];
    static size_t count_;
    static size_t passed_;
    static size_t failed_;
    static size_t test_count_;
    static size_t test_failed_;
};

class TestBase {
public:
    TestBase(const char* name) : name_(name) {}
    virtual ~TestBase() = default;

    void execute() {
        setUp();
        run();
        tearDown();
    }

    virtual void setUp() {}
    virtual void run() = 0;
    virtual void tearDown() {}

    const char* name() const { return name_; }

protected:
    void fail(const char* file, int line, const char* expr) {
        failed_ = true;
        Registry::record_failure(file, line, expr);
    }
    void pass() {
        Registry::record_success();
    }

    const char* name_;
    bool failed_ = false;
};

void run_all();
void run_debug();
void run_release();
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
        kernel::test::Registry::record_success();                              \
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

#define JARVIS_REGISTER_TEST_FLAGS(name, flags_)                              \
    do {                                                                       \
        static constexpr kernel::test::TestCase tc = {                         \
            "", #name, test_##name, nullptr, flags_                            \
        };                                                                     \
        kernel::test::Registry::register_test(tc);                             \
    } while (0)

#define JARVIS_REGISTER_TEST(name)                                            \
    JARVIS_REGISTER_TEST_FLAGS(name, kernel::test::TF_KERNEL)

#define JARVIS_REGISTER_RELEASE_TEST(name)                                    \
    JARVIS_REGISTER_TEST_FLAGS(name, kernel::test::TF_RELEASE)

#define JARVIS_REGISTER_TEST_SUITE_FLAGS(suite, name, flags_)                 \
    do {                                                                       \
        static constexpr kernel::test::TestCase tc = {                         \
            #suite, #name, test_##suite##_##name, nullptr, flags_              \
        };                                                                     \
        kernel::test::Registry::register_test(tc);                             \
    } while (0)

#define JARVIS_REGISTER_TEST_SUITE(suite, name)                                \
    JARVIS_REGISTER_TEST_SUITE_FLAGS(suite, name, kernel::test::TF_KERNEL)

#define JARVIS_REGISTER_RELEASE_TEST_SUITE(suite, name)                       \
    JARVIS_REGISTER_TEST_SUITE_FLAGS(suite, name, kernel::test::TF_RELEASE)

#define TEST_CLASS(name)                                                      \
    class name : public kernel::test::TestBase {                              \
    public:                                                                   \
        name() : TestBase(#name) {}                                           \
        void run() override;                                                  \
    };                                                                        \
    static kernel::test::TestBase* _factory_##name() { return new name(); }   \
    void name::run()

#define REGISTER_CLASS_FLAGS(name, flags_)                                    \
    do {                                                                      \
        static constexpr kernel::test::TestCase tc = {                        \
            "", #name, nullptr, _factory_##name, flags_                       \
        };                                                                    \
        kernel::test::Registry::register_test(tc);                            \
    } while (0)

#define REGISTER_CLASS(name)                                                  \
    REGISTER_CLASS_FLAGS(name, kernel::test::TF_KERNEL)

#define REGISTER_RELEASE_CLASS(name)                                          \
    REGISTER_CLASS_FLAGS(name, kernel::test::TF_RELEASE)

#define CT_ASSERT(cond)                                                       \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fail(__FILE__, __LINE__, #cond);                                  \
            return;                                                           \
        }                                                                     \
        pass();                                                               \
    } while (0)
