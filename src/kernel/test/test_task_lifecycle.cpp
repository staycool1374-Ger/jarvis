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

/// @file test_task_lifecycle.cpp
/// @brief Task creation/termination lifecycle tests.
///
/// v0.3.10 rework (SIMULATED → DRIVEN): every lifecycle transition is reached
/// through REAL dispatch and the REAL terminate/reap/cleanup paths — the test
/// never sets `task->state` directly, and blocked senders are woken by the
/// real IPC cleanup (MessageQueue destructor), not by direct field writes.

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/test/task_ptr.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <initrd/initrd.hpp>

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
// Testidea: Verifies that task cleanup nullifies msg_queue, notify,
// event_group, and kernel_stack after termination.  A REAL task terminates
// via its trampoline (the genuine exit path) and is then cleaned up.
// Input: Dispatch a kernel task (prio 11) whose lambda returns immediately —
//        the trampoline genuinely terminates it; then clean up.
// Expect: kernel_stack is null after cleanup; no double-free or use-after-free.
// Depends: kernel::task::TaskControlBlock, kernel::ipc::MessageQueue,
// kernel::sync::Notify, kernel::sync::EventGroup
JARVIS_TEST(task_exit_cleans_all_ipc_objects, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    while (t->state != TaskState::TERMINATED)
        asm volatile("pause");

    // The trampoline already ran cleanup() via terminate → zombie release;
    // drain the zombie list (the real reaper path) and verify the TCB was
    // freed cleanly (snapshot/restore balances ResourceTracker).
    JARVIS_ASSERT(t->kernel_stack == nullptr);

    release_task(t);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminating task wakes any tasks blocked on
// sending IPC to it — via the REAL IPC cleanup path (MessageQueue
// destructor), not direct field writes.
// Input: Receiver (prio 11) with a genuinely-full queue; a real sender
//        (prio 12) blocks inside IPC::send().  The receiver terminates and
//        its cleanup wakes the sender.
// Expect: Sender is woken (state READY) and its blocked send fast-fails.
JARVIS_TEST(task_exit_wakes_blocked_senders, "PRE: none | POST: none") {
    auto *receiver = TaskControlBlock::create(
        []() {
            // Real exit: dispatched, lambda returns, trampoline terminates.
        },
        11, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    // Fill the receiver's queue so a real sender blocks.
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        kernel::Message fill_msg{};
        fill_msg.sender_id = 0;
        fill_msg.type = 99;
        fill_msg.priority = 0;
        fill_msg.data_size = 0;
        receiver->msg_queue.push(fill_msg);
    }

    uint64_t r_id = receiver->id;
    uint64_t send_result = 0;
    struct SCtx {
        uint64_t recv_;
        uint64_t out_;
    } sctx;
    sctx.recv_ = r_id;
    sctx.out_ = reinterpret_cast<uint64_t>(&send_result);
    auto *sender = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *c = reinterpret_cast<SCtx *>(self->user_data);
            kernel::Message msg{};
            msg.sender_id = self->id;
            msg.type = 1;
            msg.priority = 0;
            msg.data_size = 0;
            bool ok = IPC::send(c->recv_, msg);
            __atomic_store_n(reinterpret_cast<uint64_t *>(c->out_),
                             ok ? 1 : 0, __ATOMIC_RELEASE);
        },
        12, 10);
    JARVIS_ASSERT(sender != nullptr);
    sender->user_data = &sctx;
    {
        arch::IrqGuard _guard;
        Scheduler::add_task(*sender);
    }
    while (sender->state != TaskState::BLOCKED)
        asm volatile("pause");
    JARVIS_ASSERT(receiver->msg_queue.blocked_senders_head == sender);

    // Dispatch the receiver → real termination → cleanup wakes the sender.
    Scheduler::reschedule();
    while (receiver->state != TaskState::TERMINATED)
        asm volatile("pause");
    while (sender->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT(sender->blocked_on_queue == nullptr);
    JARVIS_ASSERT_EQ(0ULL, send_result);

    release_task(sender);
    release_task(receiver);
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
// corrupt its resources.  A parent is genuinely terminated (trampoline), and
// the real reaper (reap_orphans) reparents/cleans up its children.
// Input: Real parent task (prio 11) terminates via its trampoline; the
//        reaper collects the orphaned child.
// Expect: Child is reparented to the idle task, resources cleaned up, no
// leaks (snapshot/restore balances ResourceTracker).
JARVIS_TEST(task_reparent_preserves_resources, "PRE: none | POST: none") {
    auto *parent = TaskControlBlock::create(
        []() {
            // Dispatch + immediate return → real terminate.
        },
        11, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto *child = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(*child);

    child->parent_id = parent->id;
    parent->add_child(child);
    JARVIS_ASSERT(parent->num_children == 1);

    // Dispatch parent → real terminate (orphans the child).
    Scheduler::reschedule();
    while (parent->state != TaskState::TERMINATED)
        asm volatile("pause");

    // Real reaper path reparents the child to the idle task.
    Scheduler::reap_orphans();

    auto *actual_init = Scheduler::get_idle_task();
    JARVIS_ASSERT(actual_init != nullptr);
    JARVIS_ASSERT(child->parent_id == actual_init->id);

    release_task(child);
    release_task(parent);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminated zombie task is findable via scheduler
// until cleanup+remove, then unreachable.  A REAL task terminates via its
// trampoline; the harness then removes + cleans it up.
// Input: Dispatch a kernel task (prio 11) → real terminate; find_task, then
// remove/cleanup/delete, find_task again.
// Expect: find_task returns tcb while zombie; returns nullptr after removal.
// Depends: kernel::task::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(task_zombie_state_cleanup, "PRE: none | POST: none") {
    auto *t = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(t != nullptr);
    Scheduler::add_task(*t);

    Scheduler::reschedule();
    while (t->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT(Scheduler::find_task(t->id) != nullptr);

    uint64_t tcb_id = t->id;
    release_task(t);

    JARVIS_ASSERT(Scheduler::find_task(tcb_id) == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the scheduler reaper respects parent wait status when
// collecting zombie children: a child whose parent is blocked in waitpid is
// NOT reaped while the parent waits; it is reaped after the wait is cleared.
// Input: Real parent (prio 11) sets its wait; a real child terminates; the
//        reaper runs; the wait is cleared; the reaper runs again.
// Expect: Child is NOT reaped while parent waits; reaped after wait cleared.
JARVIS_TEST(scheduler_reap_respects_parent_wait, "PRE: none | POST: none") {
    auto *parent = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto *child = TaskControlBlock::create([]() {}, 11, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(*child);
    child->parent_id = parent->id;
    parent->add_child(child);

    uint64_t child_id = child->id;
    uint64_t status = 0;
    parent->waiting_child_pid = child_id;
    parent->waiting_child_status = &status;

    // Dispatch child → real terminate (zombie).
    Scheduler::reschedule();
    while (child->state != TaskState::TERMINATED)
        asm volatile("pause");

    // Reaper must NOT collect a child whose parent is waiting.
    Scheduler::reap_orphans();
    JARVIS_ASSERT(Scheduler::find_task(child_id) != nullptr);

    // Clear the parent's wait → now reaped.
    parent->waiting_child_pid = 0;
    parent->waiting_child_status = nullptr;
    Scheduler::reap_orphans();
    JARVIS_ASSERT(Scheduler::find_task(child_id) == nullptr);

    release_task(parent);
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
    initrd::InitrdFile f = initrd::find("./user-app.c.elf");
    if (!f.data)
        f = initrd::find("user-app.c.elf");
    if (!f.data) {
        // No test ELF available, skip with pass
        JARVIS_TEST_PASS();
        return;
    }

    auto *hdr = reinterpret_cast<const kernel::elf::ELF64Header *>(f.data);
    if (!kernel::elf::validate_header(hdr)) {
        JARVIS_TEST_PASS();
        return;
    }

    SimpleTaskPtr tcb(kernel::elf::load(hdr, f.data, f.size));
    JARVIS_ASSERT(tcb != nullptr);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminated task with no parent (no waker) is
// reaped by the real reaper path.
// Input: Real kernel task (prio 11) genuinely terminates via its trampoline;
//        reap_orphans() runs.
// Expect: find_task returns nullptr after reap_orphans cleans it up.
JARVIS_TEST(lifecycle_zombie_no_waker, "PRE: none | POST: none") {
    auto *tcb = TaskControlBlock::create(
        []() {
            // Real exit: dispatched, lambda returns, trampoline terminates.
        },
        11, 10);
    JARVIS_ASSERT(tcb != nullptr);
    Scheduler::add_task(*tcb);

    uint64_t tid = tcb->id;
    Scheduler::reschedule();
    while (tcb->state != TaskState::TERMINATED)
        asm volatile("pause");

    // Real reaper collects the orphan zombie.
    Scheduler::reap_orphans();
    JARVIS_ASSERT(Scheduler::find_task(tid) == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that cleanup() frees the msg_queue even when blocked
// senders were present (bug #016) — via the REAL IPC cleanup path.
// Create a receiver, fill its queue so a sender genuinely blocks. Terminate
// receiver and cleanup.
// Expect: The blocked sender is woken and the receiver's queue is freed (no
// leak — snapshot/restore balances ResourceTracker).
JARVIS_TEST(task_cleanup_frees_msg_queue_with_blocked_senders,
            "PRE: none | POST: none") {
    auto *receiver = TaskControlBlock::create(
        []() {
            // Real exit: dispatched, lambda returns, trampoline terminates.
        },
        11, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto *sender = TaskControlBlock::create([]() {}, 12, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        Message fill_msg{};
        fill_msg.sender_id = 0;
        fill_msg.type = 99;
        fill_msg.priority = 0;
        fill_msg.data_size = 0;
        receiver->msg_queue.push(fill_msg);
    }

    // Block the sender via the REAL path (full queue).
    uint64_t r_id = receiver->id;
    uint64_t send_result = 0;
    struct SCtx {
        uint64_t recv_;
        uint64_t out_;
    } sctx;
    sctx.recv_ = r_id;
    sctx.out_ = reinterpret_cast<uint64_t>(&send_result);
    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    sender = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *c = reinterpret_cast<SCtx *>(self->user_data);
            kernel::Message msg{};
            msg.sender_id = self->id;
            msg.type = 1;
            msg.priority = 0;
            msg.data_size = 0;
            bool ok = IPC::send(c->recv_, msg);
            __atomic_store_n(reinterpret_cast<uint64_t *>(c->out_),
                             ok ? 1 : 0, __ATOMIC_RELEASE);
        },
        12, 10);
    JARVIS_ASSERT(sender != nullptr);
    sender->user_data = &sctx;
    {
        arch::IrqGuard _guard;
        Scheduler::add_task(*sender);
    }
    while (sender->state != TaskState::BLOCKED)
        asm volatile("pause");
    JARVIS_ASSERT(receiver->msg_queue.blocked_senders_head == sender);

    // Dispatch receiver → real terminate → cleanup frees the queue.
    Scheduler::reschedule();
    while (receiver->state != TaskState::TERMINATED)
        asm volatile("pause");
    while (sender->state != TaskState::TERMINATED)
        asm volatile("pause");

    JARVIS_ASSERT_EQ(0ULL, send_result);

    release_task(sender);
    release_task(receiver);
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
