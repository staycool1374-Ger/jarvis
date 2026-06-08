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

void register_scheduler_tests() {
    Logger::info("Registering scheduler tests");
    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);
    JARVIS_REGISTER_TEST(scheduler_reschedule_noop);
    JARVIS_REGISTER_TEST(scheduler_remove_task);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans);
}

JARVIS_TEST(scheduler_reap_orphans_can_reap_deferred) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    parent->waiting_child_pid = static_cast<uint64_t>(-1);
    Scheduler::add_task(parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = parent->id;
    child->state = TaskState::TERMINATED;
    child->exit_code = 0;
    Scheduler::add_task(child);

    uint64_t cnt = Scheduler::task_count();
    Scheduler::reap_orphans();
    JARVIS_ASSERT_EQ(cnt, Scheduler::task_count());

    parent->waiting_child_pid = 0;
    Scheduler::reap_orphans();
    JARVIS_ASSERT_EQ(cnt - 1, Scheduler::task_count());

    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_idle_task_userspace_exists) {
    uint64_t count = Scheduler::task_count();
    bool found = false;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            found = true;
            break;
        }
    }
    JARVIS_ASSERT(found);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_idle_task_runs_in_ring3) {
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            JARVIS_ASSERT(t->page_table_ != VMM::get_kernel_pml4());
            JARVIS_TEST_PASS();
            return;
        }
    }
}

JARVIS_TEST(kernel_hlt_idle_remains_fallback) {
    JARVIS_ASSERT(Scheduler::idle_task_ != nullptr);
    JARVIS_ASSERT(Scheduler::idle_task_->kernel_stack != nullptr);
    JARVIS_ASSERT(Scheduler::idle_task_->user_stack_ == 0);
    JARVIS_ASSERT_EQ(Scheduler::idle_task_, Scheduler::task_at(0));
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
    JARVIS_REGISTER_TEST(scheduler_idle_task_userspace_exists);
    JARVIS_REGISTER_TEST(scheduler_idle_task_runs_in_ring3);
    JARVIS_REGISTER_TEST(kernel_hlt_idle_remains_fallback);
}
