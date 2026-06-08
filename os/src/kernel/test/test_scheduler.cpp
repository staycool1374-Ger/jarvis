#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(scheduler_task_count) {
    uint64_t cnt = Scheduler::task_count();
    JARVIS_ASSERT(cnt >= 1);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_current_task) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->id > 0);
    JARVIS_TEST_PASS();
}

void register_scheduler_tests() {
    Logger::info("Registering scheduler tests");
    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);
}
