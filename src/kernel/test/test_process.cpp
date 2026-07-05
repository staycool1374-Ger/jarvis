/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#if defined(CONFIG_ARCH_X86_64)
#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <constants.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Validates that add_child() correctly links a child into the parent's
// child list and increments num_children.
// Input: Create parent and child via TaskControlBlock::create()
// Expect: parent->num_children == 1, child is in parent's child list, child->parent_id == parent->id
// Depends: test, scheduler
JARVIS_TEST(process_add_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);

    parent->add_child(child);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    auto* found = parent->find_child(child->id);
    JARVIS_ASSERT(found == child);
    JARVIS_ASSERT_EQ(parent->id, child->parent_id);

    child->cleanup();
    delete child;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that find_child returns nullptr when searching for a
// non-existent child PID.
// Input: PID 999999 (non-existent)
// Expect: JARVIS_ASSERT checks that find_child returns nullptr
// Depends: test, scheduler
JARVIS_TEST(process_find_child, "PRE: none | POST: none") {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    auto* child = parent->find_child(999999);
    JARVIS_ASSERT(child == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that the current task's num_children count is
// accessible and non-negative (implicitly true for unsigned).
// Input: Current task from Scheduler::current_task()
// Expect: JARVIS_ASSERT checks parent non-null and num_children is accessible
// Depends: test, scheduler
JARVIS_TEST(process_num_children_count, "PRE: none | POST: none") {
    auto* parent = Scheduler::current_task();
    JARVIS_ASSERT(parent != nullptr);

    // num_children is unsigned, so it's implicitly >= 0
    // Just verify it's accessible
    (void)parent->num_children;
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
JARVIS_TEST(process_remove_child_non_child_no_underflow, "PRE: none | POST: none") {
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
// Runmode: kernel
// Testidea: Validates that find_child correctly returns the child when multiple
// children exist.
// Input: Parent with 3 children, search for middle child by PID
// Expect: find_child returns correct child pointer
// Depends: test, scheduler
JARVIS_TEST(process_find_child_multiple, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    auto* child3 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child3 != nullptr);

    parent->add_child(child1);
    parent->add_child(child2);
    parent->add_child(child3);
    JARVIS_ASSERT_EQ(3ULL, parent->num_children);

    auto* found = parent->find_child(child2->id);
    JARVIS_ASSERT(found == child2);

    found = parent->find_child(child1->id);
    JARVIS_ASSERT(found == child1);

    found = parent->find_child(child3->id);
    JARVIS_ASSERT(found == child3);

    // Cleanup
    child3->cleanup();
    delete child3;
    child2->cleanup();
    delete child2;
    child1->cleanup();
    delete child1;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that remove_child correctly updates sibling links when
// removing the first child (head of list).
// Input: Parent with 3 children, remove the first added (which is head)
// Expect: num_children decremented, remaining children linked correctly
// Depends: test, scheduler
JARVIS_TEST(process_remove_first_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    auto* child3 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child3 != nullptr);

    parent->add_child(child1);
    parent->add_child(child2);
    parent->add_child(child3);
    JARVIS_ASSERT_EQ(3ULL, parent->num_children);

    // child3 is head (last added), child2 is middle, child1 is tail
    // Remove child3 (head)
    parent->remove_child(child3);
    JARVIS_ASSERT_EQ(2ULL, parent->num_children);
    JARVIS_ASSERT(parent->first_child == child2);
    JARVIS_ASSERT(child2->prev_sibling == nullptr);
    JARVIS_ASSERT(child2->next_sibling == child1);
    JARVIS_ASSERT(child1->prev_sibling == child2);
    JARVIS_ASSERT(child1->next_sibling == nullptr);

    // Cleanup
    child3->cleanup();
    delete child3;
    child2->cleanup();
    delete child2;
    child1->cleanup();
    delete child1;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that remove_child correctly updates sibling links when
// removing a middle child.
// Input: Parent with 3 children, remove the middle one
// Expect: num_children decremented, head and tail linked correctly
// Depends: test, scheduler
JARVIS_TEST(process_remove_middle_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    auto* child3 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child3 != nullptr);

    parent->add_child(child1);
    parent->add_child(child2);
    parent->add_child(child3);
    JARVIS_ASSERT_EQ(3ULL, parent->num_children);

    // Remove child2 (middle)
    parent->remove_child(child2);
    JARVIS_ASSERT_EQ(2ULL, parent->num_children);
    JARVIS_ASSERT(parent->first_child == child3);
    JARVIS_ASSERT(child3->prev_sibling == nullptr);
    JARVIS_ASSERT(child3->next_sibling == child1);
    JARVIS_ASSERT(child1->prev_sibling == child3);
    JARVIS_ASSERT(child1->next_sibling == nullptr);

    // Cleanup
    child2->cleanup();
    delete child2;
    child3->cleanup();
    delete child3;
    child1->cleanup();
    delete child1;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that remove_child correctly updates sibling links when
// removing the last child (tail of list).
// Input: Parent with 3 children, remove the last added (tail)
// Expect: num_children decremented, remaining children linked correctly
// Depends: test, scheduler
JARVIS_TEST(process_remove_last_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    auto* child3 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child3 != nullptr);

    parent->add_child(child1);
    parent->add_child(child2);
    parent->add_child(child3);
    JARVIS_ASSERT_EQ(3ULL, parent->num_children);

    // Remove child1 (tail)
    parent->remove_child(child1);
    JARVIS_ASSERT_EQ(2ULL, parent->num_children);
    JARVIS_ASSERT(parent->first_child == child3);
    JARVIS_ASSERT(child3->prev_sibling == nullptr);
    JARVIS_ASSERT(child3->next_sibling == child2);
    JARVIS_ASSERT(child2->prev_sibling == child3);
    JARVIS_ASSERT(child2->next_sibling == nullptr);

    // Cleanup
    child1->cleanup();
    delete child1;
    child3->cleanup();
    delete child3;
    child2->cleanup();
    delete child2;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that clone() properly adds the child to parent's child list.
// Input: Create user parent, set as current, clone
// Expect: parent->num_children == 1, child is in parent's list, child->parent_id == parent->id
// Depends: test, scheduler, task
JARVIS_TEST(process_clone_adds_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*parent);

    uint64_t regs[22] = {};
    regs[17] = 0x1000;
    regs[18] = arch::SEG_USER_CODE;
    regs[19] = arch::RFLAGS_DEFAULT;
    regs[20] = 0x80000000;
    regs[21] = arch::SEG_USER_DATA;

    auto* child = TaskControlBlock::clone(regs);
    JARVIS_ASSERT(child != nullptr);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    auto* found = parent->find_child(child->id);
    JARVIS_ASSERT(found == child);
    JARVIS_ASSERT_EQ(parent->id, child->parent_id);

    child->cleanup();
    delete child;

    Scheduler::remove_task(*parent);
    parent->cleanup();
    delete parent;
    if (original) Scheduler::set_current(*original);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that cleanup() removes the task from its parent's child list.
// Input: Parent with child, call cleanup() on child
// Expect: parent->num_children == 0, parent->first_child == nullptr
// Depends: test, scheduler, task
JARVIS_TEST(process_cleanup_removes_from_parent, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);

    parent->add_child(child);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    child->cleanup();
    JARVIS_ASSERT_EQ(0ULL, parent->num_children);
    JARVIS_ASSERT(parent->first_child == nullptr);
    JARVIS_ASSERT_EQ(0ULL, child->parent_id);

    Scheduler::remove_task(*parent);
    delete child;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that num_children never underflows when removing
// non-existent children multiple times.
// Input: Parent with one child, remove it, then try to remove again
// Expect: num_children stays at 0
// Depends: test, scheduler
JARVIS_TEST(process_remove_child_twice_no_underflow, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);

    parent->add_child(child);
    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    parent->remove_child(child);
    JARVIS_ASSERT_EQ(0ULL, parent->num_children);

    // Try to remove again
    parent->remove_child(child);
    JARVIS_ASSERT_EQ(0ULL, parent->num_children);

    child->cleanup();
    delete child;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates that removing a child sets child's parent_id to 0.
// Input: Parent with child, remove child
// Expect: child->parent_id == 0
// Depends: test, scheduler
JARVIS_TEST(process_remove_child_clears_parent_id, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);

    parent->add_child(child);
    JARVIS_ASSERT_EQ(parent->id, child->parent_id);

    parent->remove_child(child);
    JARVIS_ASSERT_EQ(0ULL, child->parent_id);

    child->cleanup();
    delete child;
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

void register_process_tests() {
    Logger::info("Registering process tests");
    JARVIS_REGISTER_TEST(process_add_child);
    JARVIS_REGISTER_TEST(process_find_child);
    JARVIS_REGISTER_TEST(process_find_child_multiple);
    JARVIS_REGISTER_TEST(process_num_children_count);
    JARVIS_REGISTER_TEST(process_remove_child_non_child_no_underflow);
    JARVIS_REGISTER_TEST(process_remove_first_child);
    JARVIS_REGISTER_TEST(process_remove_middle_child);
    JARVIS_REGISTER_TEST(process_remove_last_child);
    JARVIS_REGISTER_TEST(process_clone_adds_child);
    JARVIS_REGISTER_TEST(process_cleanup_removes_from_parent);
    JARVIS_REGISTER_TEST(process_remove_child_twice_no_underflow);
    JARVIS_REGISTER_TEST(process_remove_child_clears_parent_id);
}
#endif
