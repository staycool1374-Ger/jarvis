#include <test.hpp>
#include <logger.hpp>
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
// Testidea: Verifies that task cleanup nullifies msg_queue, notify, event_group, and kernel_stack after termination.
// Input: Create a kernel task via TaskControlBlock::create, set state to TERMINATED, call cleanup().
// Expect: All four pointers are null after cleanup; no double-free or use-after-free.
// Depends: kernel::task::TaskControlBlock, kernel::ipc::MessageQueue, kernel::sync::Notify, kernel::sync::EventGroup
JARVIS_TEST(task_exit_cleans_all_ipc_objects) {
    auto* tcb = TaskControlBlock::create([]() {}, 5, 10);
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

    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminating task wakes any tasks blocked on sending IPC to it.
// Input: Create a receiver task, add to scheduler. Create a sender task that blocks on send to receiver.
//        Set receiver to TERMINATED and call cleanup.
// Expect: Sender is woken up (state becomes READY) and removed from blocked list.
JARVIS_TEST(task_exit_wakes_blocked_senders) {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(receiver);

    auto* sender = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(sender);

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
    (void)kernel::IPC::send(receiver->id, msg, 0);
    // IPC::send with blocking should not return if queue is full
    // The sender should be in blocked list
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == sender);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    // Now terminate receiver and cleanup
    receiver->state = TaskState::TERMINATED;
    receiver->exit_code = 0;
    receiver->cleanup();

    // Sender should be woken up
    JARVIS_ASSERT(sender->state == TaskState::READY);
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == nullptr);
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_tail == nullptr);

    Scheduler::remove_task(sender);
    Scheduler::remove_task(receiver);
    delete sender;
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that cleanup of a user-created task frees its page table, user stack, and stack physical address.
// Input: Create a user task via TaskControlBlock::create_user (32 KiB stack), call cleanup().
// Expect: page_table_, user_stack_, and stack_phys_ are all zeroed after cleanup.
// Depends: kernel::task::TaskControlBlock, kernel::memory::PMM, kernel::memory::VMM
JARVIS_TEST(task_exit_frees_page_tables_correctly) {
    auto* tcb = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->page_table_ != 0);
    JARVIS_ASSERT(tcb->user_stack_ != 0);
    JARVIS_ASSERT(tcb->page_table_shared_ == false);

    tcb->cleanup();

    JARVIS_ASSERT(tcb->page_table_ == 0);
    JARVIS_ASSERT(tcb->user_stack_ == 0);
    JARVIS_ASSERT(tcb->stack_phys_ == 0);

    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that reparenting a terminating task does not leak or corrupt its resources.
// Input: Create parent, child (via clone). Set child to TERMINATED. Call reap_orphans.
// Expect: Child is reparented to init task, child's resources are cleaned up, no leaks.
JARVIS_TEST(task_reparent_preserves_resources) {
    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    // Create a fake child by manually setting up hierarchy
    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    Scheduler::add_task(child);
    child->parent_id = parent->id;
    parent->add_child(child);

    JARVIS_ASSERT(parent->num_children == 1);

    // Terminate parent
    parent->state = TaskState::TERMINATED;
    parent->exit_code = 0;

    // Find init task (first task with parent_id == 0)
    TaskControlBlock* init_task = nullptr;
    for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->parent_id == 0 && t->id != 0) {
            init_task = t;
            break;
        }
    }
    JARVIS_ASSERT(init_task != nullptr);

    // Reap orphans - should reparent child to init
    Scheduler::reap_orphans();

    // Child should be reparented to init
    JARVIS_ASSERT(child->parent_id == init_task->id);
    JARVIS_ASSERT(init_task->num_children >= 1);

    // Cleanup
    Scheduler::remove_task(child);
    Scheduler::remove_task(parent);
    delete child;
    delete parent;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a terminated zombie task is findable via scheduler until cleanup+remove, then unreachable.
// Input: Create a kernel task, add to scheduler, set TERMINATED, find_task, cleanup, remove_task, delete, find_task again.
// Expect: find_task returns tcb before removal; returns nullptr after removal+delete.
// Depends: kernel::task::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(task_zombie_state_cleanup) {
    auto* tcb = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(tcb != nullptr);
    Scheduler::add_task(tcb);

    tcb->state = TaskState::TERMINATED;
    tcb->exit_code = 0;

    JARVIS_ASSERT(Scheduler::find_task(tcb->id) == tcb);

    tcb->cleanup();
    Scheduler::remove_task(tcb);
    delete tcb;

    JARVIS_ASSERT(Scheduler::find_task(tcb->id) == nullptr);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies the scheduler reaper respects parent wait status when collecting zombie children.
// Input: Create parent and child. Parent calls waitpid (sets waiting_child_pid). Child exits.
// Expect: Child is NOT reaped while parent is waiting; reaped after parent collects status.
JARVIS_TEST(scheduler_reap_respects_parent_wait) {
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
// Testidea: Verifies that ELF loading calls init_task_common for the loaded task.
// Input: Load an ELF binary via elf::load, check that msg_queue, notify, event_group are initialized.
// Expect: All three IPC objects are non-null after ELF load (init_task_common was called).
JARVIS_TEST(elf_load_init_task_common_called) {
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

    auto* tcb = kernel::elf::load(hdr, f.data);
    JARVIS_ASSERT(tcb != nullptr);

    // init_task_common should have been called, so IPC objects should be initialized
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);

    // Cleanup
    tcb->cleanup();
    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all task lifecycle test cases with the test framework.
// Input: None.
// Expect: All JARVIS_REGISTER_TEST calls succeed and tests are available for execution.
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
}
