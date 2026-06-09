#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that a child process can be added to a parent's child list.
// Input: (none)
// Expect: JARVIS_TEST_PASS()
// Depends: test, scheduler
JARVIS_TEST(process_add_child) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that find_child returns nullptr when searching for a non-existent child PID.
// Input: PID 999999 (non-existent)
// Expect: JARVIS_ASSERT checks that find_child returns nullptr
// Depends: test, scheduler
JARVIS_TEST(process_find_child) {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    auto* child = parent->find_child(999999);
    JARVIS_ASSERT(child == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that the current task's num_children count is non-negative.
// Input: Current task from Scheduler::current_task()
// Expect: JARVIS_ASSERT checks parent non-null and num_children >= 0
// Depends: test, scheduler
JARVIS_TEST(process_num_children_count) {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    JARVIS_ASSERT(parent->num_children >= 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all process-related unit tests with the test framework.
// Input: (none)
// Expect: Each JARVIS_REGISTER_TEST call registers a test function for later execution
// Depends: test, logger, scheduler
void register_process_tests() {
    Logger::info("Registering process tests");
    JARVIS_REGISTER_TEST(process_add_child);
    JARVIS_REGISTER_TEST(process_find_child);
    JARVIS_REGISTER_TEST(process_num_children_count);
}
