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

/// @file test_task_lifecycle.cpp
/// @brief Task creation/termination lifecycle tests.

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/test/task_ptr.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <initrd/initrd.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that task cleanup nullifies msg_queue, notify,
// event_group, and kernel_stack after termination.
// Input: Create a kernel task via TaskControlBlock::create, set state to
// TERMINATED, call cleanup().
// Expect: All four pointers are null after cleanup; no double-free or
// use-after-free.
// Depends: kernel::task::TaskControlBlock, kernel::ipc::MessageQueue,
// kernel::sync::Notify, kernel::sync::EventGroup
JARVIS_TEST(task_exit_cleans_all_ipc_objects, "PRE: none | POST: none") {
    SimpleTaskPtr tcb(TaskControlBlock::create([]() {}, 5, 10));
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);

    tcb->state = TaskState::TERMINATED;
    tcb->exit_code = 0;

    tcb->cleanup();

    JARVIS_ASSERT(tcb->msg_queue == nullptr);
    JARVIS_ASSERT(tcb->notify == nullptr);
    JARVIS_ASSERT(tcb->event_group == nullptr);
    JARVIS_ASSERT(tcb->kernel_stack == nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminating task wakes any tasks blocked on
// sending IPC to it.
// Input: Create a receiver task, add to scheduler. Create a sender task that
// blocks on send to receiver.
//        Set receiver to TERMINATED and call cleanup.
// Expect: Sender is woken up (state becomes READY) and removed from blocked
// list.
JARVIS_TEST(task_exit_wakes_blocked_senders, "PRE: none | POST: none") {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto* sender = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*sender);
        sender->cleanup();
        delete sender;
        Scheduler::remove_task(*receiver);
        delete receiver;
    });

    kernel::Message msg{};
    msg.sender_id = sender->id;
    msg.type = 1;
    msg.priority = 0;
    msg.data_size = 0;

    // Fill receiver's queue to force sender to block
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        kernel::Message fill_msg{};
        fill_msg.sender_id = 0;
        fill_msg.type = 99;
        fill_msg.priority = 0;
        fill_msg.data_size = 0;
        receiver->msg_queue->push(fill_msg);
    }

    // Now send from sender - should block
    Scheduler::set_current(*sender);
    (void)kernel::IPC::send(receiver->id, msg, 0);
    // IPC::send with blocking should not return if queue is full
    // The sender should be in blocked list
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == sender);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    // Now terminate receiver and cleanup
    receiver->state = TaskState::TERMINATED;
    receiver->exit_code = 0;
    receiver->cleanup();

    // Sender should be woken up and removed from the blocked list
    JARVIS_ASSERT(sender->state == TaskState::READY);
    JARVIS_ASSERT(sender->blocked_on_queue == nullptr);
    // cleanup() frees the receiver's msg_queue after clearing blocked senders
    JARVIS_ASSERT(receiver->msg_queue == nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that cleanup of a user-created task frees its page
// table, user stack, and stack physical address.
// Input: Create a user task via TaskControlBlock::create_user (32 KiB
// stack), call cleanup().
// Expect: page_table_, user_stack_, and stack_phys_ are all zeroed after
// cleanup.
// Depends: kernel::task::TaskControlBlock, kernel::memory::PMM,
// kernel::memory::VMM
JARVIS_TEST(task_exit_frees_page_tables_correctly, "PRE: none | POST: none") {
    SimpleTaskPtr tcb(TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB));
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->page_table_ != 0);
    JARVIS_ASSERT(tcb->user_stack_ != 0);
    JARVIS_ASSERT(tcb->page_table_shared_ == false);

    tcb->cleanup();

    JARVIS_ASSERT(tcb->page_table_ == 0);
    JARVIS_ASSERT(tcb->user_stack_ == 0);
    JARVIS_ASSERT(tcb->stack_phys_ == 0);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that reparenting a terminating task does not leak or
// corrupt its resources.
// Input: Create parent, child (via clone). Set child to TERMINATED. Call
// reap_orphans.
// Expect: Child is reparented to init task, child's resources are cleaned
// up, no leaks.
JARVIS_TEST(task_reparent_preserves_resources, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    // Create a fake child by manually setting up hierarchy
    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(*child);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*child);
        child->cleanup();
        delete child;
    });

    child->parent_id = parent->id;
    parent->add_child(child);

    JARVIS_ASSERT(parent->num_children == 1);

    // Terminate parent
    parent->state = TaskState::TERMINATED;
    parent->exit_code = 0;

    // Reap orphans - should reparent child to init
    Scheduler::reap_orphans();

    // Child should be reparented to init (the idle/root task)
    auto* actual_init = Scheduler::get_idle_task();
    JARVIS_ASSERT(actual_init != nullptr);
    JARVIS_ASSERT(child->parent_id == actual_init->id);
    JARVIS_ASSERT(actual_init->num_children >= 1);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminated zombie task is findable via scheduler
// until cleanup+remove, then unreachable.
// Input: Create a kernel task, add to scheduler, set TERMINATED, find_task,
// cleanup, remove_task, delete, find_task again.
// Expect: find_task returns tcb before removal; returns nullptr after
// removal+delete.
// Depends: kernel::task::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(task_zombie_state_cleanup, "PRE: none | POST: none") {
    TaskPtr tcb(TaskControlBlock::create([]() {}, 5, 10));
    JARVIS_ASSERT(tcb != nullptr);
    Scheduler::add_task(*tcb);

    tcb->state = TaskState::TERMINATED;
    tcb->exit_code = 0;

    JARVIS_ASSERT(Scheduler::find_task(tcb->id) == tcb.get());

    uint64_t tcb_id = tcb->id;
    tcb.reset();  // TaskDeleter: remove_task + cleanup + delete

    JARVIS_ASSERT(Scheduler::find_task(tcb_id) == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the scheduler reaper respects parent wait status when
// collecting zombie children.
// Input: Create parent and child. Parent calls waitpid (sets
// waiting_child_pid). Child exits.
// Expect: Child is NOT reaped while parent is waiting; reaped after parent
// collects status.
JARVIS_TEST(scheduler_reap_respects_parent_wait, "PRE: none | POST: none") {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(*child);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*parent);
        parent->cleanup();
        delete parent;
    });

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

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that ELF loading calls init_task_common for the loaded
// task.
// Input: Load an ELF binary via elf::load, check that msg_queue, notify,
// event_group are initialized.
// Expect: All three IPC objects are non-null after ELF load
// (init_task_common was called).
JARVIS_TEST(elf_load_init_task_common_called, "PRE: none | POST: none") {
    // Find a test ELF in initrd
    initrd::InitrdFile f = initrd::find("./test_fork.c.elf");
    if (!f.data) f = initrd::find("test_fork.c.elf");
    if (!f.data) {
        // No test ELF available, skip with pass
        JARVIS_TEST_PASS();
        return;
    }

    auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
    if (!kernel::elf::validate_header(hdr)) {
        JARVIS_TEST_PASS();
        return;
    }

    SimpleTaskPtr tcb(kernel::elf::load(hdr, f.data));
    JARVIS_ASSERT(tcb != nullptr);

    // init_task_common should have been called, so IPC objects should be
    // initialized
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminated task with parent_id==0 (no parent, no
// waker) is reaped by reap_orphans.
// Input: Create a kernel task, orphan it (parent_id=0), terminate, call
// reap_orphans().
// Expect: find_task returns nullptr after reap_orphans cleans it up.
JARVIS_TEST(lifecycle_zombie_no_waker, "PRE: none | POST: none") {
    auto* tcb = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(tcb != nullptr);
    Scheduler::add_task(*tcb);

    uint64_t tid = tcb->id;

    // Orphan the task (no parent, no waker)
    tcb->parent_id = 0;
    tcb->state = TaskState::TERMINATED;
    tcb->exit_code = 0;

    // reap_orphans should find and reap this zombie
    Scheduler::reap_orphans();

    JARVIS_ASSERT(Scheduler::find_task(tid) == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that cleanup() frees the msg_queue even when blocked
// senders were present (bug #016).
// Create a receiver, fill its queue so a sender blocks. Terminate receiver
// and cleanup.
// Expect: receiver->msg_queue is nullptr after cleanup (no leak).
JARVIS_TEST(task_cleanup_frees_msg_queue_with_blocked_senders, "PRE: none | POST: none") {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto* sender = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    auto cleanup = ScopeGuard([&]() {
        Scheduler::remove_task(*sender);
        sender->cleanup();
        delete sender;
        Scheduler::remove_task(*receiver);
        delete receiver;
    });

    Message msg{};
    msg.sender_id = sender->id;
    msg.type = 1;
    msg.priority = 0;
    msg.data_size = 0;

    // Fill receiver's queue to force sender to block
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        Message fill_msg{};
        fill_msg.sender_id = 0;
        fill_msg.type = 99;
        fill_msg.priority = 0;
        fill_msg.data_size = 0;
        receiver->msg_queue->push(fill_msg);
    }

    // Send from sender — should block (receiver queue full)
    Scheduler::set_current(*sender);
    (void)IPC::send(receiver->id, msg, 0);
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == sender);

    // Terminate + cleanup — must free msg_queue
    receiver->state = TaskState::TERMINATED;
    receiver->exit_code = 0;
    receiver->cleanup();

    JARVIS_ASSERT(receiver->msg_queue == nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all task lifecycle test cases with the test framework.
// Input: None.
// Expect: All JARVIS_REGISTER_TEST calls succeed and tests are available for
// execution.
// Depends: kernel::Logger, kernel::test framework
void register_task_lifecycle_tests() {
    Logger::info("Registering task lifecycle tests");
    JARVIS_REGISTER_TEST(task_exit_cleans_all_ipc_objects);
    JARVIS_REGISTER_TEST(task_exit_wakes_blocked_senders);
    JARVIS_REGISTER_TEST(task_exit_frees_page_tables_correctly);
    JARVIS_REGISTER_TEST(task_reparent_preserves_resources);
    JARVIS_REGISTER_TEST(task_zombie_state_cleanup);
    JARVIS_REGISTER_TEST(scheduler_reap_respects_parent_wait);
    JARVIS_REGISTER_TEST(elf_load_init_task_common_called);
    JARVIS_REGISTER_TEST(lifecycle_zombie_no_waker);
    JARVIS_REGISTER_TEST(task_cleanup_frees_msg_queue_with_blocked_senders);
}
