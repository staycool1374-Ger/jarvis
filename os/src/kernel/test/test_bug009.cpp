#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Simulates the zombie-child scenario: parent forks child1, waitpid blocks
//   (child not yet TERMINATED), child1 runs and exits leaving a zombie. Then child2 is
//   added. A second waitpid must reap child1 (the zombie) — not child2 — proving that
//   child2 would never be waited on in the original bug.
// Input: Create parent, child1 (TERMINATED), then child2 (READY).
// Expect: Simulated second waitpid finds and reaps child1. child2 remains in scheduler.
JARVIS_TEST(bug009_zombie_reaps_first) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    // Child 1 — simulate that it ran and exited (TERMINATED)
    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    child1->parent_id = parent->id;
    child1->state = TaskState::TERMINATED;
    child1->exit_code = 42;
    parent->add_child(child1);
    Scheduler::add_task(child1);

    // Simulate first waitpid: parent blocked, child1 was not reaped.
    // (This is what happened before the fix — waitpid returned -1.)

    // Now add child2 — simulating the second fork
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    child2->parent_id = parent->id;
    child2->state = TaskState::READY;
    parent->add_child(child2);
    Scheduler::add_task(child2);

    // Simulate second waitpid(-1, ...): iterate scheduler tasks looking for
    // a terminated child of this parent — exactly what sys_waitpid does.
    TaskControlBlock* reaped = nullptr;
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->parent_id == parent->id && t->state == TaskState::TERMINATED) {
            reaped = t;
            break;
        }
    }

    // The zombie (child1) must be found — NOT child2
    JARVIS_ASSERT(reaped == child1);
    JARVIS_ASSERT(reaped != child2);

    // Verify child1 is still in the scheduler (hasn't been reaped yet)
    JARVIS_ASSERT(Scheduler::find_task(child1->id) == child1);

    // Now actually reap child1
    parent->remove_child(child1);
    child1->cleanup();
    Scheduler::remove_task(child1);
    delete child1;

    // Child1 should be gone from scheduler
    JARVIS_ASSERT(Scheduler::find_task(child1->id) == nullptr);

    // Child2 should still be present
    JARVIS_ASSERT(Scheduler::find_task(child2->id) == child2);
    JARVIS_ASSERT(child2->state == TaskState::READY);

    // Cleanup
    Scheduler::remove_task(child2);
    delete child2;
    Scheduler::remove_task(parent);
    delete parent;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Normal two-child waitpid: both children are TERMINATED and properly
//   reaped by sequential waitpid calls.
// Input: Create parent and two children, both TERMINATED.
// Expect: First waitpid reaps child1, second waitpid reaps child2. No zombies remain.
JARVIS_TEST(bug009_two_children_sequential_reap) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    child1->parent_id = parent->id;
    child1->state = TaskState::TERMINATED;
    child1->exit_code = 10;
    parent->add_child(child1);
    Scheduler::add_task(child1);

    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    child2->parent_id = parent->id;
    child2->state = TaskState::TERMINATED;
    child2->exit_code = 20;
    parent->add_child(child2);
    Scheduler::add_task(child2);

    // First waitpid: find and reap child1
    TaskControlBlock* found = nullptr;
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->parent_id == parent->id && t->state == TaskState::TERMINATED) {
            found = t;
            break;
        }
    }
    JARVIS_ASSERT(found == child1);
    parent->remove_child(child1);
    child1->cleanup();
    Scheduler::remove_task(child1);
    delete child1;
    JARVIS_ASSERT(Scheduler::find_task(child1->id) == nullptr);

    // Second waitpid: find and reap child2
    found = nullptr;
    count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->parent_id == parent->id && t->state == TaskState::TERMINATED) {
            found = t;
            break;
        }
    }
    JARVIS_ASSERT(found == child2);
    parent->remove_child(child2);
    child2->cleanup();
    Scheduler::remove_task(child2);
    delete child2;
    JARVIS_ASSERT(Scheduler::find_task(child2->id) == nullptr);

    Scheduler::remove_task(parent);
    delete parent;
    JARVIS_TEST_PASS();
}

void register_bug009_tests() {
    Logger::info("Registering bug #009 waitpid/scheduler tests");
    JARVIS_REGISTER_TEST(bug009_zombie_reaps_first);
    JARVIS_REGISTER_TEST(bug009_two_children_sequential_reap);
}
