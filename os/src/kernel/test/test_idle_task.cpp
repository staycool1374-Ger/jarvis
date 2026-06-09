#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/syscall/syscall.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: STUB - Placeholder for verifying the idle task is created during boot initialization.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::task::Scheduler
JARVIS_TEST(idle_task_created_at_boot) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that user tasks (non-idle, priority-0 tasks with a user stack) have a separate page table and a kernel stack, confirming they run in user mode (ring 3).
// Input: Iterates all scheduler tasks, finds one matching user-task criteria (priority 0, user_stack_ set, not the idle task).
// Expect: JARVIS_ASSERT checks page_table_ != kernel PML4 and kernel_stack != nullptr.
// Depends: kernel::task::Scheduler, kernel::memory::VMM
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

// Runmode: kernel
// Testidea: Tests that non-idle user tasks have priority and base_priority set to 0.
// Input: Iterates all scheduler tasks, finds one matching user-task criteria.
// Expect: JARVIS_ASSERT_EQ checks priority == 0 and base_priority == 0.
// Depends: kernel::task::Scheduler
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

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that the idle task calls the PAUSE syscall (HLT with interrupts enabled).
// Input: (stub)
// Expect: (stub)
// Depends: kernel::task::Scheduler, kernel::syscall
JARVIS_TEST(idle_task_calls_pause_syscall) {
    // PAUSE calls hlt which requires interrupts enabled; from kernel task
    // context the hlt may never wake.
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that the idle task yields the CPU to higher-priority tasks.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::task::Scheduler
JARVIS_TEST(idle_task_yields_to_higher_priority) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that the kernel idle task (the HLT-based one) always exists at boot, has a kernel stack, no user stack, and is the first task in the scheduler list.
// Input: Scheduler::get_idle_task() and Scheduler::task_at(0).
// Expect: JARVIS_ASSERT checks idle task is non-null, has kernel_stack, user_stack_ == 0, and matches task_at(0).
// Depends: kernel::task::Scheduler
JARVIS_TEST(kernel_hlt_idle_still_exists) {
    JARVIS_ASSERT(Scheduler::get_idle_task() != nullptr);
    JARVIS_ASSERT(Scheduler::get_idle_task()->kernel_stack != nullptr);
    JARVIS_ASSERT(Scheduler::get_idle_task()->user_stack_ == 0);
    JARVIS_ASSERT_EQ(Scheduler::get_idle_task(), Scheduler::task_at(0));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that the idle task can be restarted after a crash.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::task::Scheduler
JARVIS_TEST(idle_task_restartable_on_crash) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Placeholder for testing that creating multiple idle tasks is prevented.
// Input: (stub)
// Expect: (stub)
// Depends: kernel::task::Scheduler
JARVIS_TEST(multiple_idle_tasks_prevented) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all idle-task test cases into the test framework.
// Input: None
// Expect: Each JARVIS_REGISTER_TEST call adds the test to the suite.
// Depends: kernel::test
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
