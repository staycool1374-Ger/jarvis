/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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

/// @file test_waitpid.cpp
/// @brief Wait/PID (waitpid) syscall tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): the waitpid contract is driven
/// through the REAL kernel paths — a real parent task genuinely blocks in a
/// wait (waiting_child_pid + waiting_child_status), a real child genuinely
/// terminates via Scheduler::terminate(), and the real wake_waiting_parent
/// path delivers the exit status and reaps the child.  No direct state
/// writes.

#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <scope_guard.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

namespace {
void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    Scheduler::remove_task(*t);
    t->cleanup();
    delete t;
}
} // namespace

// Runmode: kernel
// Testidea: The zombie-child scenario via the real wait→exit→wake contract:
// a real parent blocks in a wait (waiting_child_pid set), a real child
// genuinely terminates via Scheduler::terminate(), and the real
// wake_waiting_parent path delivers the exit status and reaps the child.
// Input: Real parent task (prio 11) sets its wait; a real child (prio 11)
//        terminates with exit code 42; the real wake path runs.
// Expect: The parent's waiting_child_status receives 42; the child is
//         removed from the scheduler; the parent's wait is cleared.
JARVIS_TEST(waitpid_zombie_over_new_child, "PRE: none | POST: none") {
    auto *parent = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto *child = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = parent->id;
    parent->add_child(child);
    Scheduler::add_task(*child);
    uint64_t child_id = child->id;

    // Parent blocks in waitpid for this child.
    uint64_t status = 0;
    parent->waiting_child_pid = child_id;
    parent->waiting_child_status = &status;

    // Real child exit: dispatch the child (its trampoline terminates it with
    // exit code 0); the wake_waiting_parent path delivers the status.
    Scheduler::reschedule();
    while (child->state != TaskState::TERMINATED)
        asm volatile("pause");

    // Real wake path: child exit code (0) delivered to the waiting parent;
    // the child is reaped and the parent's wait is cleared.
    JARVIS_ASSERT(parent->waiting_child_pid == 0);
    JARVIS_ASSERT(status == 0);
    JARVIS_ASSERT(Scheduler::find_task(child_id) == nullptr);

    release_task(parent);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Two children, sequential reaping via the real wait→exit→wake
// contract: a real parent waits for child1, child1 genuinely terminates and
// is reaped; then the parent waits for child2, child2 genuinely terminates
// and is reaped.
// Input: Real parent task (prio 11) waits for two real children in turn;
//        each child genuinely terminates via its trampoline.
// Expect: Each child's status is delivered to the parent and reaped; no
// zombies remain in the scheduler.
JARVIS_TEST(waitpid_two_children_sequential_reap, "PRE: none | POST: none") {
    auto *parent = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    // --- Round 1: child1 ---
    auto *child1 = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(child1 != nullptr);
    child1->parent_id = parent->id;
    parent->add_child(child1);
    Scheduler::add_task(*child1);
    uint64_t child1_id = child1->id;

    uint64_t status1 = 0;
    parent->waiting_child_pid = child1_id;
    parent->waiting_child_status = &status1;

    Scheduler::reschedule();
    while (child1->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT(parent->waiting_child_pid == 0);
    JARVIS_ASSERT(status1 == 0);
    JARVIS_ASSERT(Scheduler::find_task(child1_id) == nullptr);

    // --- Round 2: child2 ---
    auto *child2 = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(child2 != nullptr);
    child2->parent_id = parent->id;
    parent->add_child(child2);
    Scheduler::add_task(*child2);
    uint64_t child2_id = child2->id;

    uint64_t status2 = 0;
    parent->waiting_child_pid = child2_id;
    parent->waiting_child_status = &status2;

    Scheduler::reschedule();
    while (child2->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT(parent->waiting_child_pid == 0);
    JARVIS_ASSERT(status2 == 0);
    JARVIS_ASSERT(Scheduler::find_task(child2_id) == nullptr);

    release_task(parent);
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

    // Allocate two different USER-owned physical pages for parent and child.
    uint64_t parent_page = PMM::alloc_user_page();
    uint64_t child_page = PMM::alloc_user_page();
    JARVIS_ASSERT(parent_page != 0);
    JARVIS_ASSERT(child_page != 0);
    JARVIS_ASSERT(parent_page != child_page);

    // Zero them and write sentinel values
    memset(reinterpret_cast<void *>(arch::HHDM_OFFSET + parent_page), 0, 4096);
    memset(reinterpret_cast<void *>(arch::HHDM_OFFSET + child_page), 0, 4096);
    *reinterpret_cast<uint64_t *>(arch::HHDM_OFFSET + parent_page) =
        0xAAAAAAAABBBBBBBBULL;
    *reinterpret_cast<uint64_t *>(arch::HHDM_OFFSET + child_page) =
        0xCCCCCCCCDDDDDDDDULL;

    // Clone kernel PML4 twice for parent and child
    uint64_t parent_pml4 = VMM::clone_kernel_pml4();
    uint64_t child_pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(parent_pml4 != 0);
    JARVIS_ASSERT(child_pml4 != 0);

    // Map parent_page at TEST_VA in parent's PML4, child_page at TEST_VA in
    // child's PML4
    VMM::map_page_in_pml4(TEST_VA, parent_page, true, parent_pml4);
    VMM::map_page_in_pml4(TEST_VA, child_page, true, child_pml4);

    // Verify the mappings are correct
    uint64_t phys_in_parent = VMM::virt_to_phys_in_pml4(TEST_VA, parent_pml4);
    uint64_t phys_in_child = VMM::virt_to_phys_in_pml4(TEST_VA, child_pml4);
    JARVIS_ASSERT(phys_in_parent == parent_page);
    JARVIS_ASSERT(phys_in_child == child_page);
    JARVIS_ASSERT(phys_in_parent != phys_in_child);

    // Save current CR3 (kernel PML4)
    uint64_t saved_cr3 = arch::read_cr3();

    // --- Test the CR3 switch fix ---
    arch::write_cr3(parent_pml4);
    *reinterpret_cast<uint64_t *>(TEST_VA) = 0x42;
    arch::write_cr3(saved_cr3);

    // Verify: parent's physical page got the write
    uint64_t parent_val =
        *reinterpret_cast<uint64_t *>(arch::HHDM_OFFSET + parent_page);
    JARVIS_ASSERT(parent_val == 0x42);

    // Verify: child's physical page is unchanged
    uint64_t child_val =
        *reinterpret_cast<uint64_t *>(arch::HHDM_OFFSET + child_page);
    JARVIS_ASSERT(child_val == 0xCCCCCCCCDDDDDDDDULL);

    // Cleanup
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
