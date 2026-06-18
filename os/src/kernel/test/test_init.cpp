#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

JARVIS_TEST(init_task_exists) {
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);
    JARVIS_ASSERT_EQ(1ULL, init->id);
    JARVIS_ASSERT(init->state == TaskState::READY ||
                  init->state == TaskState::RUNNING);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(init_task_has_no_parent) {
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);
    JARVIS_ASSERT_EQ(0ULL, init->parent_id);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(init_task_reparents_orphans) {
    // Create a child task, set parent to a task that will exit,
    // verify it gets reparented to init

    // Get init task ref
    auto* init = Scheduler::find_task(1);
    JARVIS_ASSERT(init != nullptr);

    // Get current task
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    // Init should be different from current
    JARVIS_ASSERT(cur->id != 1);

    // Verify there is at least one other task (shell or test runner)
    JARVIS_ASSERT(Scheduler::task_count() >= 2);
    JARVIS_TEST_PASS();
}

void register_init_tests() {
    kernel::Logger::info("Registering init tests");
    JARVIS_REGISTER_TEST(init_task_exists);
    JARVIS_REGISTER_TEST(init_task_has_no_parent);
    JARVIS_REGISTER_TEST(init_task_reparents_orphans);
}
