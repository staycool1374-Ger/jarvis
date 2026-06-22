#include <kernel/test/test_watchdog.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/timer.hpp>

namespace kernel::test {

uint64_t g_watchdog_deadline = 0;
static char g_test_name[128] = {};

void watchdog_arm(uint64_t ms, const char* test_name) {
    uint64_t now = arch::Timer::ticks();
    g_watchdog_deadline = now + ms;
    // Copy the test name into a static buffer so it's safe to access from the
    // timer ISR even if the caller's stack frame is gone.
    for (int i = 0; i < 127; ++i) {
        char c = test_name[i];
        g_test_name[i] = c;
        if (c == '\0') break;
    }
    g_test_name[127] = '\0';
}

void watchdog_disarm() {
    g_watchdog_deadline = 0;
}

void watchdog_panic() {
    g_watchdog_deadline = 0;

    Logger::raw_write("\n[WATCHDOG] Test timed out while running \"");
    Logger::raw_write(g_test_name);
    Logger::raw_write("\":\n");

    auto* task = Scheduler::current_task();
    if (task) {
        Logger::raw_write("  task_id=");
        Logger::print_dec(task->id);
        Logger::raw_write(" state=");
        Logger::print_dec(static_cast<uint64_t>(task->state));
        Logger::raw_write(" context.rip=0x");
        Logger::print_hex(task->context.rip);
        Logger::raw_write("\n");
    } else {
        Logger::raw_write("  (no current task)\n");
    }

    uint64_t rip;
    asm volatile("lea (%%rip), %0" : "=r"(rip));
    Logger::raw_write("  current_rip=0x");
    Logger::print_hex(rip);
    Logger::raw_write("\n");

    Logger::raw_write("[WATCHDOG] Halting.\n");
    arch::qemu_debug_exit(1);
    while (1) { arch::hlt(); }
}

} // namespace kernel::test
