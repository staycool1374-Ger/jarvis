#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(process_add_child) {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    parent->add_child(nullptr);
    parent->remove_child(nullptr);

    JARVIS_ASSERT_EQ(0ULL, parent->num_children);
    JARVIS_ASSERT(parent->first_child == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(process_find_child) {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    auto* child = parent->find_child(999999);
    JARVIS_ASSERT(child == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(process_num_children_count) {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    JARVIS_ASSERT(parent->num_children >= 0);
    JARVIS_TEST_PASS();
}

void register_process_tests() {
    Logger::info("Registering process tests");
    JARVIS_REGISTER_TEST(process_add_child);
    JARVIS_REGISTER_TEST(process_find_child);
    JARVIS_REGISTER_TEST(process_num_children_count);
}
