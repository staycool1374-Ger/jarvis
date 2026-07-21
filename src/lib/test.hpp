/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <types.hpp>
#include <logger.hpp>

namespace kernel {

// Forward declaration of the scheduler task type, so test.hpp can reference it
// in helper signatures without pulling in scheduler.hpp (the full definition
// is available at call sites that include <kernel/task/scheduler.hpp>).
class TaskControlBlock;

namespace test {

// Local alias so helpers below can write `TaskControlBlock` and resolve to the
// real kernel::TaskControlBlock declared above.
using TaskControlBlock = kernel::TaskControlBlock;

enum TestFlags : uint8_t {
    TF_KERNEL   = 0,
    TF_RELEASE  = 1 << 0,
    TF_USER     = 1 << 1,
    TF_BENCH    = 1 << 2,
};

class TestBase;

struct TestCase {
    const char* suite;
    const char* name;
    void (*func)();
    TestBase* (*factory)();
    uint8_t flags;
};

struct ClassSection {
    const char* name;
    size_t start;
    size_t count;
};

class Registry {
public:
    static void init();
    static void register_test(const TestCase& tc);
    static const TestCase* tests();
    static size_t count();
    static size_t class_count();
    static const ClassSection* class_section(size_t i);

    static void record_failure(const char* file, int line, const char* expr);
    static void record_failure_fmt(const char* file, int line, const char* fmt, ...);
    static void record_success();
    static void record_test(bool passed);

    static size_t passed();
    static size_t failed();
    static size_t total();
    static size_t test_count();
    static size_t test_passed();
    static size_t test_failed();
    static void reset();
    static void clear();

    static void set_expected_count(size_t n);
    static size_t expected_count();

    static void record_class_section(const char* name, size_t start, size_t count);

    // Tracks the name of the test case currently executing.  Used by
    // add_task_named() to tag test-created tasks with their origin so leaks
    // are traceable to a specific test.
    static void set_current_test_name(const char* name);
    static const char* current_test_name();

private:
    static constexpr size_t MAX_TESTS = 1024;
    static constexpr size_t MAX_CLASSES = 64;
    // NOLINTBEGIN(bugprone-dynamic-static-initializers)
    static TestCase tests_[MAX_TESTS];
    static size_t count_;
    static size_t passed_;
    static size_t failed_;
    static size_t test_count_;
    static size_t test_failed_;
    static ClassSection sections_[MAX_CLASSES];
    static size_t class_count_;
    static size_t expected_count_;
    static const char* current_name_;
    // NOLINTEND(bugprone-dynamic-static-initializers)
};

class TestBase {
public:
    TestBase(const char* name) : name_(name) {}
    virtual ~TestBase() = default;

    void execute() {
        Registry::set_current_test_name(name_);
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

/// @brief Creates a task (via the kernel's TaskControlBlock::create) and adds
///        it to the scheduler, tagging its `name` with the calling test case so
///        leaked/orphaned tasks are traceable to their origin.  `tag` (if
///        non-null) is prefixed to the test name (e.g. a role like "worker").
///        Falls back to the raw `create` name when no test is active.
TaskControlBlock *create_named_task(void (*entry)(), uint64_t priority,
                                     uint64_t period_ticks,
                                     const char *tag = nullptr);

/// @brief Adds an already-created task to the scheduler, tagging its `name`
///        with the calling test case (see create_named_task).  Thin wrapper
///        over Scheduler::add_task that records provenance.
void add_task_named(TaskControlBlock &task, const char *tag = nullptr);

void run_all();
void run_safe();
void run_filtered(uint8_t required_flags, bool use_isolation = true);
void run_debug();
void run_release();
void run_registered(uint8_t required_flags);
void run_suite(const char* suite_name);
void print_report(uint64_t start_ns, uint64_t end_ns);

void set_kernel_entry_ns();
void set_class_auto_shutdown(bool enabled);
// NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
extern bool g_class_auto_shutdown;

struct TestClass {
    const char* name;
    void (*register_all)();
};

bool register_class(const char* name);
void dump_class_counts();

} // namespace test
} // namespace kernel

#define JARVIS_TEST(name, ...)                                                \
    void test_##name();                                                        \
    void test_##name()

#define JARVIS_TEST_SUITE(suite, name, ...)                                   \
    void test_##suite##_##name();                                              \
    void test_##suite##_##name()

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

#define JARVIS_FAIL(fmt, ...)                                                  \
    do {                                                                       \
        kernel::test::Registry::record_failure_fmt(                            \
            __FILE__, __LINE__, fmt, ##__VA_ARGS__);                           \
        return;                                                                \
    } while (0)

#define JARVIS_ASSERT_FMT(cond, fmt, ...)                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            kernel::test::Registry::record_failure_fmt(                        \
                __FILE__, __LINE__, fmt, ##__VA_ARGS__);                       \
            return;                                                            \
        }                                                                      \
        kernel::test::Registry::record_success();                              \
    } while (0)

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

// NOLINTBEGIN(bugprone-macro-parentheses)
#ifndef __clang__
#define TEST_CLASS_DIAG_PUSH    _Pragma("GCC diagnostic push")
#define TEST_CLASS_DIAG_IGNORE  _Pragma("GCC diagnostic ignored \"-Wanalyzer-possible-null-dereference\"")
#define TEST_CLASS_DIAG_POP     _Pragma("GCC diagnostic pop")
#else
#define TEST_CLASS_DIAG_PUSH
#define TEST_CLASS_DIAG_IGNORE
#define TEST_CLASS_DIAG_POP
#endif

#define TEST_CLASS(name)                                                      \
    class name : public kernel::test::TestBase {                              \
    public:                                                                   \
        name() : TestBase(#name) {}                                           \
        void run() override;                                                  \
    };                                                                        \
    TEST_CLASS_DIAG_PUSH                                                      \
    TEST_CLASS_DIAG_IGNORE                                                    \
    static kernel::test::TestBase* _factory_##name() { return new name(); }   \
    TEST_CLASS_DIAG_POP                                                       \
    void name::run()
// NOLINTEND(bugprone-macro-parentheses)

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
