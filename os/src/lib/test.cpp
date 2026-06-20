/// @file test.cpp
/// @brief Kernel test framework implementation.

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/test/test_isolate.hpp>
#include <kernel/arch/io.hpp>

namespace kernel {
namespace test {

TestCase Registry::tests_[MAX_TESTS];
size_t Registry::count_ = 0;
size_t Registry::passed_ = 0;
size_t Registry::failed_ = 0;
size_t Registry::test_count_ = 0;
size_t Registry::test_failed_ = 0;
ClassSection Registry::sections_[MAX_CLASSES] = {};
size_t Registry::class_count_ = 0;
bool g_class_auto_shutdown = false;

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
size_t Registry::class_count() { return class_count_; }
const ClassSection* Registry::class_section(size_t i) {
    return (i < class_count_) ? &sections_[i] : nullptr;
}

void Registry::record_failure(const char* file, int line, const char* expr) {
    ++failed_;
    Logger::error("[TEST:FAIL] %s:%d: %s", file, line, expr);
}

void Registry::record_failure_fmt(const char* file, int line, const char* fmt, ...) {
    ++failed_;
    Logger::raw_write("\033[1;31m[TEST:FAIL] \033[0m");
    Logger::raw_write(file);
    Logger::raw_write(":");
    Logger::print_dec(line);
    Logger::raw_write(": ");
    __va_list args;
    va_start(args, fmt);
    Logger::vprint_raw(fmt, args);
    va_end(args);
    Logger::raw_write("\n");
}

void Registry::record_success() {
    ++passed_;
}

void Registry::record_test(bool passed) {
    ++test_count_;
    if (!passed) ++test_failed_;
}

void Registry::record_class_section(const char* name, size_t start, size_t count) {
    if (class_count_ >= MAX_CLASSES) return;
    sections_[class_count_].name  = name;
    sections_[class_count_].start = start;
    sections_[class_count_].count = count;
    ++class_count_;
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
        Logger::raw_write("  Tasks after: ");
        for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
            auto* t = Scheduler::task_at(i);
            Logger::raw_write("[");
            Logger::print_dec(i);
            Logger::raw_write("]id=");
            Logger::print_dec(t ? t->id : 0);
            Logger::raw_write(" ");
        }
        Logger::raw_write("\n");
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
            Logger::raw_write("\033[1;31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }
    }

    print_report();
}

void run_safe() {
    Registry::reset();
    size_t n = Registry::count();
    if (n == 0) {
        Logger::warn("No tests registered");
        return;
    }

    Logger::info("[TEST:RUN] Running test(s) (safe mode)");

    for (size_t i = 0; i < n; ++i) {
        auto& tc = Registry::tests()[i];
        if (tc.flags & TF_USER) continue;
        if (!(tc.flags & TF_RELEASE)) continue;

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
            Logger::raw_write("\033[1;31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }
    }

    print_report();
}

void run_filtered(uint8_t required_flags, bool use_isolation) {
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
    bool snapshot_ok = false;
    if (use_isolation && n > 0) {
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

        for (size_t ci = 0; ci < Registry::class_count(); ++ci) {
            auto* cs = Registry::class_section(ci);
            if (cs && i == cs->start) {
                Logger::raw_write("\n--- ");
                Logger::raw_write(cs->name);
                Logger::raw_write(" ---\n");
            }
        }

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
            Logger::raw_write("\033[1;31m[FAIL]\033[0m\n");
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
    } else {
        // No snapshot isolation — still need to clean up test-created tasks,
        // VFS state, and restart daemons before continuing to the shell.
        kernel::test::reload_daemon_tasks();
    }

    if (run_count == 0) {
        Logger::warn("No tests matched flags 0x%x", required_flags);
    }

    print_report();
}

void run_debug() {
    run_filtered(0, true);
}

void run_release() {
    run_filtered(TF_RELEASE, false);
}

void run_registered(uint8_t required_flags) {
    run_filtered(required_flags, true);
    if (g_class_auto_shutdown) {
        uint64_t result = (Registry::test_failed() == 0) ? 0 : 1;
        arch::qemu_debug_exit(result);
    }
}

void set_class_auto_shutdown(bool enabled) {
    g_class_auto_shutdown = enabled;
}

void run_suite(const char* suite_name) {
    Registry::reset();
    size_t n = Registry::count();

    Logger::info("[TEST:RUN] Suite: %s", suite_name);

    // Take snapshot once before the suite for isolation
    bool snapshot_ok = true;
    if (n > 0) {
        snapshot_ok = kernel::test::snapshot_create();
        if (!snapshot_ok) {
            Logger::warn("Test snapshot creation failed — isolation disabled");
        }
    }

    size_t run = 0;
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
            Logger::raw_write("\033[1;31m[FAIL]\033[0m\n");
            Registry::record_test(false);
        }

        // Restore kernel state between tests (tasks, MemPool, page tables)
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
        kernel::test::reload_daemon_tasks();
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
    Logger::raw_write("  Failed: \033[1;31m"); write_num(tf); Logger::raw_write("\033[0m\n");
    Logger::raw_write("==============================\n\n");
}

} // namespace test
} // namespace kernel
