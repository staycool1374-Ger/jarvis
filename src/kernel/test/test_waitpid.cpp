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

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Simulates the zombie-child scenario: parent forks child1,
// waitpid blocks
// (child not yet TERMINATED), child1 runs and exits leaving a zombie. Then
// child2 is
// added. A second waitpid must reap child1 (the zombie) — not child2 —
// proving that
//   child2 would never be waited on in the original bug.
// Input: Create parent, child1 (TERMINATED), then child2 (READY).
// Expect: Simulated second waitpid finds and reaps child1. child2 remains in
// scheduler.
JARVIS_TEST(waitpid_zombie_over_new_child, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    // Child 1 — simulate that it ran and exited (TERMINATED)
    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    child1->parent_id = parent->id;
    child1->state = TaskState::TERMINATED;
    child1->exit_code = 42;
    parent->add_child(child1);
    Scheduler::add_task(*child1);

    // Simulate first waitpid: parent blocked, child1 was not reaped.
    // (This is what happened before the fix — waitpid returned -1.)

    // Now add child2 — simulating the second fork
    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    child2->parent_id = parent->id;
    child2->state = TaskState::READY;
    parent->add_child(child2);
    Scheduler::add_task(*child2);

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
    uint64_t child1_id = child1->id;
    parent->remove_child(child1);
    child1->cleanup();
    Scheduler::remove_task(*child1);
    delete child1;

    // Child1 should be gone from scheduler
    JARVIS_ASSERT(Scheduler::find_task(child1_id) == nullptr);

    // Child2 should still be present
    JARVIS_ASSERT(Scheduler::find_task(child2->id) == child2);
    JARVIS_ASSERT(child2->state == TaskState::READY);

    // Cleanup
    Scheduler::remove_task(*child2);
    child2->cleanup();
    delete child2;
    Scheduler::remove_task(*parent);
    parent->cleanup();
    delete parent;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Normal two-child waitpid: both children are TERMINATED and properly
//   reaped by sequential waitpid calls.
// Input: Create parent and two children, both TERMINATED.
// Expect: First waitpid reaps child1, second waitpid reaps child2. No
// zombies remain.
JARVIS_TEST(waitpid_two_children_sequential_reap, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* child1 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child1 != nullptr);
    child1->parent_id = parent->id;
    child1->state = TaskState::TERMINATED;
    child1->exit_code = 10;
    parent->add_child(child1);
    Scheduler::add_task(*child1);

    auto* child2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child2 != nullptr);
    child2->parent_id = parent->id;
    child2->state = TaskState::TERMINATED;
    child2->exit_code = 20;
    parent->add_child(child2);
    Scheduler::add_task(*child2);

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
    uint64_t child1_id = child1->id;
    parent->remove_child(child1);
    child1->cleanup();
    Scheduler::remove_task(*child1);
    delete child1;
    JARVIS_ASSERT(Scheduler::find_task(child1_id) == nullptr);

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
    uint64_t child2_id = child2->id;
    parent->remove_child(child2);
    child2->cleanup();
    Scheduler::remove_task(*child2);
    delete child2;
    JARVIS_ASSERT(Scheduler::find_task(child2_id) == nullptr);

    Scheduler::remove_task(*parent);
    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Validates the CR3 switch fix in sys_exit(). When a child task
// writes
//   exit status to the parent's user-space pointer, it must first switch to the
//   parent's page table (CR3) so the write lands in the parent's physical page,
// not the child's. This test creates two different PML4s that map the same
// user
//   virtual address to different physical pages, then proves the fix works.
// Input: Two PML4s (parent/child), each mapping VA 0x70000000 to a different
// phys page.
// Expect: After writing to VA via child's CR3 + parent CR3 switch, the parent's
//   physical page contains the write; the child's physical page is unchanged.
JARVIS_TEST(waitpid_cr3_switch_on_status_write, "PRE: none | POST: none") {
    constexpr uint64_t TEST_VA = 0x70000000;

    Logger::raw_write("waitpid58: enter\n");
    // Allocate two different physical pages for parent and child
    uint64_t parent_page = PMM::alloc_page();
    Logger::raw_write("waitpid58: alloc parent done\n");
    uint64_t child_page  = PMM::alloc_page();
    Logger::raw_write("waitpid58: alloc child done\n");
    JARVIS_ASSERT(parent_page != 0);
    JARVIS_ASSERT(child_page != 0);
    JARVIS_ASSERT(parent_page != child_page);

    // Zero them and write sentinel values
    memset(reinterpret_cast<void*>(arch::HHDM_OFFSET + parent_page), 0, 4096);
    memset(reinterpret_cast<void*>(arch::HHDM_OFFSET + child_page), 0, 4096);
    *reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + parent_page) = 0xAAAAAAAABBBBBBBBULL;
    *reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + child_page)  = 0xCCCCCCCCDDDDDDDDULL;

    // Clone kernel PML4 twice for parent and child
    uint64_t parent_pml4 = VMM::clone_kernel_pml4();
    uint64_t child_pml4  = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(parent_pml4 != 0);
    JARVIS_ASSERT(child_pml4 != 0);

    // Map parent_page at TEST_VA in parent's PML4, child_page at TEST_VA in
    // child's PML4
    VMM::map_page_in_pml4(TEST_VA, parent_page, true, parent_pml4);
    VMM::map_page_in_pml4(TEST_VA, child_page,  true, child_pml4);

    // Verify the mappings are correct: each PML4 maps TEST_VA to a different
    // physical page
    uint64_t phys_in_parent = VMM::virt_to_phys_in_pml4(TEST_VA, parent_pml4);
    uint64_t phys_in_child  = VMM::virt_to_phys_in_pml4(TEST_VA, child_pml4);
    JARVIS_ASSERT(phys_in_parent == parent_page);
    JARVIS_ASSERT(phys_in_child  == child_page);
    JARVIS_ASSERT(phys_in_parent != phys_in_child);

    // Save current CR3 (kernel PML4)
    uint64_t saved_cr3 = arch::read_cr3();

    // --- Test the CR3 switch fix ---
    // Simulate child's sys_exit: we're running in child's context (CR3 =
    // child_pml4),
    // but we need to write status to the parent's user-space address.
    // The fix: switch to parent's CR3 before the write.

    Logger::info("waitpid_cr3: starting CR3 switch sequence");
    arch::write_cr3(parent_pml4);
    *reinterpret_cast<uint64_t*>(TEST_VA) = 0x42;
    arch::write_cr3(saved_cr3);
    Logger::info("waitpid_cr3: CR3 sequence done");

    // Verify: parent's physical page got the write
    uint64_t parent_val = *reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + parent_page);
    JARVIS_ASSERT(parent_val == 0x42);

    // Verify: child's physical page is unchanged (still has its sentinel)
    uint64_t child_val = *reinterpret_cast<uint64_t*>(arch::HHDM_OFFSET + child_page);
    JARVIS_ASSERT(child_val == 0xCCCCCCCCDDDDDDDDULL);

    // Cleanup: free page tables (USER-owned, freed by free_user_pages),
    // then free leaf pages (KERNEL-owned) and PML4 pages (KERNEL-owned by
    // clone_kernel_pml4).
    VMM::free_user_pages(parent_pml4);
    VMM::free_user_pages(child_pml4);
    PMM::free_page(parent_pml4);
    PMM::free_page(child_pml4);
    PMM::free_page(parent_page);
    PMM::free_page(child_page);

    JARVIS_TEST_PASS();
}

void register_waitpid_tests() {
    Logger::info("Registering waitpid tests");
    JARVIS_REGISTER_TEST(waitpid_zombie_over_new_child);
    JARVIS_REGISTER_TEST(waitpid_two_children_sequential_reap);
    JARVIS_REGISTER_RELEASE_TEST(waitpid_cr3_switch_on_status_write);
}
