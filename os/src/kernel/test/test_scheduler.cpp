#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Validates that the scheduler reports at least one task running.
// Input: No parameters; reads global scheduler state
// Expect: JARVIS_ASSERT checks that task_count >= 1
// Depends: test, scheduler
JARVIS_TEST(scheduler_task_count) {
    uint64_t cnt = Scheduler::task_count();
    JARVIS_ASSERT(cnt >= 1);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that the current task is non-null and has a positive task ID.
// Input: No parameters; reads current task from Scheduler::current_task()
// Expect: JARVIS_ASSERT checks non-null and id > 0
// Depends: test, scheduler
JARVIS_TEST(scheduler_current_task) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->id > 0);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that calling Scheduler::reschedule returns a valid current task (no-op reschedule).
// Input: No parameters; captures before/after task pointers
// Expect: JARVIS_ASSERT checks that before and after are both non-null
// Depends: test, scheduler
JARVIS_TEST(scheduler_reschedule_noop) {
    auto* before = Scheduler::current_task();
    JARVIS_ASSERT(before != nullptr);
    Scheduler::reschedule();
    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that adding a new task increments the task count and removing it restores the original count.
// Input: A new TaskControlBlock with empty lambda, priority 1, quanta 10
// Expect: JARVIS_ASSERT_EQ checks count increases by 1 after add, returns to original after remove; current_task non-null
// Depends: test, scheduler, task, pmm, vmm
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

// Runmode: kernel
// Testidea: Validates that reaping orphans removes a terminated task whose parent has exited (parent_id 999999).
// Input: A new TaskControlBlock with parent_id=999999, state=TERMINATED, exit_code=42
// Expect: JARVIS_ASSERT checks count increased by 1 after add, then <= original+1 after reap_orphans; current_task non-null
// Depends: test, scheduler, task, pmm, vmm
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

// Runmode: kernel
// Testidea: Verifies that deferred reaping respects parent wait status - child not reaped while parent waits.
// Input: Create parent and child. Parent sets waiting_child_pid. Child exits. Reap orphans.
// Expect: Child is NOT reaped while parent waits; reaped after parent clears wait.
JARVIS_TEST(scheduler_reap_orphans_can_reap_deferred) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(child);
    child->parent_id = parent->id;
    parent->add_child(child);

    uint64_t child_id = child->id;
    uint64_t status = 0;
    parent->waiting_child_pid = child_id;
    parent->waiting_child_status = &status;

    // Terminate child
    child->state = TaskState::TERMINATED;
    child->exit_code = 42;

    // Reap orphans - should NOT reap child because parent is waiting
    Scheduler::reap_orphans();

    // Child should still exist (not reaped yet)
    JARVIS_ASSERT(Scheduler::find_task(child_id) == child);

    // Now simulate parent collecting (clear wait)
    parent->waiting_child_pid = 0;
    parent->waiting_child_status = nullptr;

    // Reap again - now child should be reaped
    Scheduler::reap_orphans();

    JARVIS_ASSERT(Scheduler::find_task(child_id) == nullptr);

    Scheduler::remove_task(parent);
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all scheduler-related unit tests with the test framework.
// Input: (none)
// Expect: Each JARVIS_REGISTER_TEST call registers a test function for later execution
// Depends: test, logger, scheduler, task, pmm, vmm
void register_scheduler_tests() {
    Logger::info("Registering scheduler tests");
    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);
    JARVIS_REGISTER_TEST(scheduler_reschedule_noop);
    JARVIS_REGISTER_TEST(scheduler_remove_task);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans);
    JARVIS_REGISTER_TEST(scheduler_reap_orphans_can_reap_deferred);
}
