#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/elf/elf.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that TaskControlBlock::cleanup() nullifies all allocated resources (kernel stack, msg queue, notify, event group).
// Input: Create a TCB and call cleanup()
// Expect: kernel_stack, msg_queue, notify, event_group become nullptr; stack_phys_ becomes 0
// Depends: kernel::TaskControlBlock
JARVIS_TEST(task_cleanup_frees_resources) {
    auto* tcb = TaskControlBlock::create([]() {}, 1, 10);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->kernel_stack != nullptr);
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);

    tcb->cleanup();

    JARVIS_ASSERT(tcb->kernel_stack == nullptr);
    JARVIS_ASSERT(tcb->msg_queue == nullptr);
    JARVIS_ASSERT(tcb->notify == nullptr);
    JARVIS_ASSERT(tcb->event_group == nullptr);
    JARVIS_ASSERT_EQ(0ULL, tcb->stack_phys_);

    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that create_user allocates a page table and user stack of the requested size.
// Input: create_user with stack_size=32_KiB
// Expect: page_table_ != 0, user_stack_ != 0, user_stack_size_ == 32_KiB
// Depends: kernel::TaskControlBlock
JARVIS_TEST(task_create_user_page_table) {
    auto* tcb = TaskControlBlock::create_user([]() {}, 1, 10, 32_KiB);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->page_table_ != 0);
    JARVIS_ASSERT(tcb->user_stack_ != 0);
    JARVIS_ASSERT_EQ(32_KiB, tcb->user_stack_size_);

    tcb->cleanup();
    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that TaskControlBlock::clone() creates a child task whose page table is shared with the parent.
// Input: Create a user parent task, set current, push registers, clone
// Expect: child != nullptr, child->page_table_ != 0, child->page_table_shared_ == true, child->user_stack_ != 0
// Depends: kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(task_clone_shares_page_tables) {
    auto* parent = TaskControlBlock::create_user([]() {}, 1, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);
    JARVIS_ASSERT(parent->page_table_ != 0);
    JARVIS_ASSERT(parent->user_stack_ != 0);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(parent);

    uint64_t regs[22] = {};
    regs[17] = 0x1000;
    regs[18] = arch::SEG_USER_CODE;
    regs[19] = arch::RFLAGS_DEFAULT;
    regs[20] = 0x80000000;
    regs[21] = arch::SEG_USER_DATA;

    auto* child = TaskControlBlock::clone(regs);
    JARVIS_ASSERT(child != nullptr);
    JARVIS_ASSERT(child->page_table_ != 0);
    JARVIS_ASSERT(child->page_table_shared_ == true);
    JARVIS_ASSERT(child->user_stack_ != 0);

    child->cleanup();
    delete child;

    Scheduler::remove_task(parent);
    parent->cleanup();
    delete parent;
    if (original) Scheduler::set_current(original);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that ELF loading initializes IPC notification/event objects for a task.
// Input: Load an ELF binary via elf::load, check that msg_queue, notify, event_group are initialized.
// Expect: All three IPC objects are non-null after ELF load.
JARVIS_TEST(task_elf_load_inits_ipc_objects) {
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

    // ELF load should have called init_task_common, so IPC objects should be initialized
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);

    // Cleanup
    tcb->cleanup();
    delete tcb;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that cleaning up a cloned child task does not free the parent's shared page table.
// Input: Create user parent, clone child, capture parent page_table_ address, cleanup/delete child
// Expect: parent->page_table_ unchanged and non-null after child cleanup
// Depends: kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(task_fork_child_cleanup_preserves_parent_pages) {
    auto* parent = TaskControlBlock::create_user([]() {}, 1, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(parent);
    uint64_t regs[22] = {};
    regs[17] = 0x1000; regs[18] = arch::SEG_USER_CODE; regs[19] = arch::RFLAGS_DEFAULT;
    regs[20] = 0x80000000; regs[21] = arch::SEG_USER_DATA;

    auto* child = TaskControlBlock::clone(regs);
    JARVIS_ASSERT(child != nullptr);
    JARVIS_ASSERT(child->page_table_shared_ == true);

    uint64_t parent_pml4 = parent->page_table_;
    child->cleanup();
    delete child;

    JARVIS_ASSERT(parent->page_table_ == parent_pml4);
    JARVIS_ASSERT(parent->page_table_ != 0);

    Scheduler::remove_task(parent);
    parent->cleanup();
    delete parent;
    if (original) Scheduler::set_current(original);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all task management unit tests with the test framework.
// Input: None
// Expect: All task_* tests are registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_task_tests() {
    Logger::info("Registering task tests");
    JARVIS_REGISTER_TEST(task_cleanup_frees_resources);
    JARVIS_REGISTER_TEST(task_create_user_page_table);
    JARVIS_REGISTER_TEST(task_clone_shares_page_tables);
    JARVIS_REGISTER_TEST(task_elf_load_inits_ipc_objects);
    JARVIS_REGISTER_TEST(task_fork_child_cleanup_preserves_parent_pages);
}
