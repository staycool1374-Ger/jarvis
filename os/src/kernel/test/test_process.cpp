#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that a child process can be added
// to a parent's child list.
// Input: (none)
// Expect: JARVIS_TEST_PASS()
// Depends: test, scheduler
JARVIS_TEST(process_add_child) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that find_child returns nullptr when searching for a
// non-existent child PID.
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
// Testidea: Validates that the current task's num_children count is
// non-negative.
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
// Testidea: Verifies that remove_child() does not modify num_children when
// the target
// is not actually a child of the parent (bug #019).
// Input: Create a parent and two tasks; add one as child; try to remove the
// non-child.
// Expect: num_children remains unchanged.
// Depends: test, scheduler
JARVIS_TEST(process_remove_child_non_child_no_underflow) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);

    auto* stranger = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(stranger != nullptr);

    parent->add_child(child);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    // Try to remove the stranger (not a child)
    parent->remove_child(stranger);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    // Removing the actual child should work
    parent->remove_child(child);
    JARVIS_ASSERT_EQ(0ULL, parent->num_children);

    child->cleanup();
    delete child;
    stranger->cleanup();
    delete stranger;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all process-related unit tests with the test framework.
// Input: (none)
// Expect: Each JARVIS_REGISTER_TEST call registers a test function for later
// execution
// Depends: test, logger, scheduler
void register_process_tests() {
    Logger::info("Registering process tests");
    JARVIS_REGISTER_TEST(process_add_child);
    JARVIS_REGISTER_TEST(process_find_child);
    JARVIS_REGISTER_TEST(process_num_children_count);
    JARVIS_REGISTER_TEST(process_remove_child_non_child_no_underflow);
}
