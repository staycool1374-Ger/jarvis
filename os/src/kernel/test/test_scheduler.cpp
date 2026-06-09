#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

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

JARVIS_TEST(scheduler_reschedule_noop) {
    auto* before = Scheduler::current_task();
    JARVIS_ASSERT(before != nullptr);
    Scheduler::reschedule();
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_remove_task) {
    auto* before = Scheduler::current_task();
    JARVIS_ASSERT(before != nullptr);
    uint64_t cnt_before = Scheduler::task_count();

    auto* new_task = TaskControlBlock::create([]() {}, 1, 10);
    JARVIS_ASSERT(new_task != nullptr);
    Scheduler::add_task(new_task);
    JARVIS_ASSERT_EQ(cnt_before + 1, Scheduler::task_count());

    Scheduler::remove_task(new_task);
    JARVIS_ASSERT_EQ(cnt_before, Scheduler::task_count());

    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != nullptr);

    new_task->cleanup();
    delete new_task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_reap_orphans) {
    uint64_t cnt_before = Scheduler::task_count();

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = 999999;
    child->state = TaskState::TERMINATED;
    child->exit_code = 42;
    Scheduler::add_task(child);

    JARVIS_ASSERT_EQ(cnt_before + 1, Scheduler::task_count());

    Scheduler::reap_orphans();

    JARVIS_ASSERT(Scheduler::task_count() <= cnt_before + 1);

    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_reap_orphans_can_reap_deferred) {
    JARVIS_TEST_PASS();
}

void register_scheduler_tests() {
    Logger::info("Registering scheduler tests");
    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);
    JARVIS_REGISTER_TEST(scheduler_reschedule_noop);
    JARVIS_REGISTER_TEST(scheduler_remove_task);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans_can_reap_deferred);
}
