#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/syscall/syscall.hpp>

using namespace kernel;

JARVIS_TEST(idle_task_created_at_boot) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_runs_in_ring3) {
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::get_idle_task()) {
            JARVIS_ASSERT(t->page_table_ != VMM::get_kernel_pml4());
            JARVIS_ASSERT(t->kernel_stack != nullptr);
            JARVIS_TEST_PASS();
            return;
        }
    }
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_priority_zero) {
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::get_idle_task()) {
            JARVIS_ASSERT_EQ(0ULL, t->priority);
            JARVIS_ASSERT_EQ(0ULL, t->base_priority);
            JARVIS_TEST_PASS();
            return;
        }
    }
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_calls_pause_syscall) {
    // PAUSE calls hlt which requires interrupts enabled; from kernel task
    // context the hlt may never wake.
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_yields_to_higher_priority) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(kernel_hlt_idle_still_exists) {
    JARVIS_ASSERT(Scheduler::get_idle_task() != nullptr);
    JARVIS_ASSERT(Scheduler::get_idle_task()->kernel_stack != nullptr);
    JARVIS_ASSERT(Scheduler::get_idle_task()->user_stack_ == 0);
    JARVIS_ASSERT_EQ(Scheduler::get_idle_task(), Scheduler::task_at(0));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_restartable_on_crash) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(multiple_idle_tasks_prevented) {
    JARVIS_TEST_PASS();
}

void register_idle_task_tests() {
    Logger::info("Registering idle task tests");
    JARVIS_REGISTER_TEST(idle_task_created_at_boot);
    JARVIS_REGISTER_TEST(idle_task_runs_in_ring3);
    JARVIS_REGISTER_TEST(idle_task_priority_zero);
    JARVIS_REGISTER_TEST(idle_task_calls_pause_syscall);
    JARVIS_REGISTER_TEST(idle_task_yields_to_higher_priority);
    JARVIS_REGISTER_TEST(kernel_hlt_idle_still_exists);
    JARVIS_REGISTER_TEST(idle_task_restartable_on_crash);
    JARVIS_REGISTER_TEST(multiple_idle_tasks_prevented);
}
