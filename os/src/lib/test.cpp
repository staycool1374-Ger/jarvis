/// @file test.cpp
/// @brief Kernel test framework implementation.

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/test/test_isolate.hpp>

namespace kernel {
namespace test {

TestCase Registry::tests_[MAX_TESTS];
size_t Registry::count_ = 0;
size_t Registry::passed_ = 0;
size_t Registry::failed_ = 0;
size_t Registry::test_count_ = 0;
size_t Registry::test_failed_ = 0;

void Registry::init() {
    count_ = 0;
    passed_ = 0;
    failed_ = 0;
    test_count_ = 0;
    test_failed_ = 0;
}

void Registry::register_test(const TestCase& tc) {
    if (count_ >= MAX_TESTS) {
        Logger::error("Test registry full (%d tests)", MAX_TESTS);
        return;
    }
    tests_[count_++] = tc;
}

const TestCase* Registry::tests() { return tests_; }
size_t Registry::count() { return count_; }

void Registry::record_failure(const char* file, int line, const char* expr) {
    ++failed_;
    Logger::error("[TEST:FAIL] %s:%d: %s", file, line, expr);
}

void Registry::record_success() {
    ++passed_;
}

void Registry::record_test(bool passed) {
    ++test_count_;
    if (!passed) ++test_failed_;
}

void Registry::reset() {
    passed_ = 0;
    failed_ = 0;
    test_count_ = 0;
    test_failed_ = 0;
}

size_t Registry::passed() { return passed_; }
size_t Registry::failed() { return failed_; }
size_t Registry::total() { return passed_ + failed_; }
size_t Registry::test_count() { return test_count_; }
size_t Registry::test_passed() { return test_count_ - test_failed_; }
size_t Registry::test_failed() { return test_failed_; }

static void run_one(const TestCase& tc) {
    // Snapshot scheduler/PMM state for leak detection
    size_t before_tasks = Scheduler::task_count();

    if (tc.factory) {
        TestBase* t = tc.factory();
        t->execute();
        delete t;
    } else {
        tc.func();
    }

    // Leak detection: warn if scheduler task count changed
    // (tests that intentionally create/destroy tasks must balance add+remove)
    size_t after_tasks = Scheduler::task_count();
    if (after_tasks != before_tasks) {
        Logger::warn("[TEST:LEAK] %s: scheduler task count changed %d -> %d",
                     tc.name, before_tasks, after_tasks);
    }
}

void run_all() {
    Registry::reset();
    size_t n = Registry::count();
    if (n == 0) {
        Logger::warn("No tests registered");
        return;
    }

    Logger::info("[TEST:RUN] Running %d test(s)", n);

    for (size_t i = 0; i < n; ++i) {
        auto& tc = Registry::tests()[i];
        size_t before_fail = Registry::failed();

        Logger::raw_write("  ");
        Logger::raw_write(tc.suite);
        if (tc.suite[0]) Logger::raw_write("::");
        Logger::raw_write(tc.name);
        Logger::raw_write(" ... ");

        run_one(tc);

        if (Registry::failed() == before_fail) {
            Logger::raw_write("\033[32m[PASS]\033[0m\n");
            Registry::record_test(true);
        } else {
            Logger::raw_write("\033[31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }
    }

    print_report();
}

void run_filtered(uint8_t required_flags) {
    Registry::reset();
    size_t n = Registry::count();
    if (n == 0) {
        Logger::warn("No tests registered");
        return;
    }

    size_t run_count = 0;
    Logger::info("[TEST:RUN] Running filtered test(s) (flags=%u)", (unsigned)required_flags);

    // Take a snapshot of the entire kernel state before the first test so
    // we can restore it between individual tests (full isolation).
    bool snapshot_ok = true;
    if (n > 0) {
        snapshot_ok = kernel::test::snapshot_create();
        if (!snapshot_ok) {
            Logger::warn("Test snapshot creation failed — isolation disabled");
        }
    }

    for (size_t i = 0; i < n; ++i) {
        auto& tc = Registry::tests()[i];
        // Match: if required_flags is 0 (debug), run all non-user tests.
        // If required_flags has TF_RELEASE set, run only tests with TF_RELEASE.
        if (tc.flags & TF_USER) continue;
        if (required_flags && !(tc.flags & required_flags)) continue;

        ++run_count;
        size_t before_fail = Registry::failed();

        Logger::raw_write("  ");
        Logger::raw_write(tc.suite);
        if (tc.suite[0]) Logger::raw_write("::");
        Logger::raw_write(tc.name);
        Logger::raw_write(" ... ");

        run_one(tc);

        if (Registry::failed() == before_fail) {
            Logger::raw_write("\033[32m[PASS]\033[0m\n");
            Registry::record_test(true);
        } else {
            Logger::raw_write("\033[31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }

        // Restore the snapshot after each test so the next test starts
        // from a clean slate (tasks, memory, daemons, page tables).
        if (snapshot_ok) {
            char test_buf[128];
            int pos = 0;
            const char* s = tc.suite;
            while (*s && pos < 120) test_buf[pos++] = *s++;
            if (pos > 0 && tc.suite[0]) { test_buf[pos++] = ':'; test_buf[pos++] = ':'; }
            s = tc.name;
            while (*s && pos < 126) test_buf[pos++] = *s++;
            test_buf[pos] = '\0';
            kernel::test::snapshot_restore(test_buf);
        }
    }

    if (snapshot_ok) {
        kernel::test::snapshot_destroy();
        // Reload daemon ELFs from initrd to replace corrupted page tables
        // (kernel-owned page table pages are not snapshot-saved, so they
        // contain test garbage after the last restore).
        kernel::test::reload_daemon_tasks();
    }

    if (run_count == 0) {
        Logger::warn("No tests matched flags 0x%x", required_flags);
    }

    print_report();
}

void run_debug() {
    run_filtered(0);
}

void run_release() {
    run_filtered(TF_RELEASE);
}

void run_suite(const char* suite_name) {
    Registry::reset();
    size_t n = Registry::count();
    size_t run = 0;

    Logger::info("[TEST:RUN] Suite: %s", suite_name);

    for (size_t i = 0; i < n; ++i) {
        auto& tc = Registry::tests()[i];
        if (tc.suite[0] == '\0') continue;
        if (strcmp(tc.suite, suite_name) != 0) continue;

        ++run;
        size_t before_fail = Registry::failed();

        Logger::raw_write("  ");
        Logger::raw_write(tc.name);
        Logger::raw_write(" ... ");

        run_one(tc);

        if (Registry::failed() == before_fail) {
            Logger::raw_write("\033[32m[PASS]\033[0m\n");
            Registry::record_test(true);
        } else {
            Logger::raw_write("\033[31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }
    }

    if (run == 0) {
        Logger::warn("No tests found for suite '%s'", suite_name);
    }

    print_report();
}

void print_report() {
    size_t tp = Registry::test_passed();
    size_t tf = Registry::test_failed();
    size_t tt = tp + tf;

    Logger::raw_write("\n");
    Logger::raw_write("==============================\n");
    Logger::raw_write(" TEST RESULTS\n");
    Logger::raw_write("==============================\n");

    auto write_num = [](size_t n) {
        char buf[24];
        int pos = 23;
        buf[23] = '\0';
        if (n == 0) { Logger::raw_write("0"); return; }
        size_t v = n;
        while (v > 0 && pos > 0) { buf[--pos] = '0' + (v % 10); v /= 10; }
        Logger::raw_write(buf + pos);
    };

    Logger::raw_write("  Total:  "); write_num(tt); Logger::raw_write("\n");
    Logger::raw_write("  Passed: \033[32m"); write_num(tp); Logger::raw_write("\033[0m\n");
    Logger::raw_write("  Failed: \033[31m"); write_num(tf); Logger::raw_write("\033[0m\n");
    Logger::raw_write("==============================\n\n");
}

} // namespace test
} // namespace kernel
