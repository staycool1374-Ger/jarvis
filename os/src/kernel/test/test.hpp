/// @file test.hpp
/// @brief Kernel self-test framework — registry, execution, and reporting.

#pragma once

#include <types.hpp>

namespace kernel {
namespace test {

/// @brief Result of a single test case.
enum class TestResult : uint8_t {
    PASS,
    FAIL,
    SKIP,
};

/// @brief Describes a single registered test case.
struct Test {
    const char* name;
    const char* description;
    TestResult (*func)();
    TestResult result;
    const char* failure_msg;
};

/// @brief Summary report from a test run.
struct TestReport {
    size_t total;
    size_t passed;
    size_t failed;
    size_t skipped;
};

/// @brief Central registry of kernel self-tests.
class TestRegistry {
public:
    /// @brief Initialises the test registry.
    static void init();

    /// @brief Registers a test case.
    /// @param name        Test identifier (used for selective runs).
    /// @param description Human-readable description.
    /// @param func        Test function returning a TestResult.
    /// @return true if registration succeeded.
    static bool register_test(const char* name, const char* description,
                              TestResult (*func)());

    /// @brief Runs all registered tests.
    /// @return Aggregate report.
    static TestReport run_all();

    /// @brief Runs a single test by name.
    /// @param name Test identifier to run.
    /// @return Report for that test (total = 1 on success, 0 if not found).
    static TestReport run(const char* name);

    /// @brief Returns the number of registered tests.
    static size_t count() { return count_; }

    /// @brief Returns the test at the given index.
    static const Test* get(size_t index) {
        return index < count_ ? &tests_[index] : nullptr;
    }

    /// @brief Finds a test by name.
    static const Test* find(const char* name);

private:
    static constexpr size_t MAX_TESTS = 200;
    static Test tests_[MAX_TESTS];
    static size_t count_;
    static bool initialized_;

    /// @brief Runs a single test and records the result.
    static void run_test(Test& t);
};

/// @brief Helper macro to register a test at file scope.
/// @note Place inside a function called from TestRegistry::init().
#define REGISTER_TEST(name, desc, func) \
    TestRegistry::register_test(name, desc, func)

/// @brief Helper to return pass/fail from a test function.
#define TEST_PASS()  return kernel::test::TestResult::PASS
#define TEST_FAIL(msg) do { \
    (void)sizeof(msg); \
    return kernel::test::TestResult::FAIL; \
} while (0)

/// @brief Assertion helper for test functions.
#define TEST_ASSERT(cond) do { \
    if (!(cond)) return kernel::test::TestResult::FAIL; \
} while (0)

} // namespace test
} // namespace kernel
