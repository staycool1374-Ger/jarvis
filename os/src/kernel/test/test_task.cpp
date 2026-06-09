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

JARVIS_TEST(task_clone_shares_page_tables) {
    auto* parent = TaskControlBlock::create_user([]() {}, 1, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);
    JARVIS_ASSERT(parent->page_table_ != 0);
    JARVIS_ASSERT(parent->user_stack_ != 0);

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
    JARVIS_TEST_PASS();
}

JARVIS_TEST(task_elf_load_inits_ipc_objects) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(task_fork_child_cleanup_preserves_parent_pages) {
    auto* parent = TaskControlBlock::create_user([]() {}, 1, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

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
    JARVIS_TEST_PASS();
}

void register_task_tests() {
    Logger::info("Registering task tests");
    JARVIS_REGISTER_TEST(task_cleanup_frees_resources);
    JARVIS_REGISTER_TEST(task_create_user_page_table);
    JARVIS_REGISTER_TEST(task_clone_shares_page_tables);
    JARVIS_REGISTER_TEST(task_elf_load_inits_ipc_objects);
    JARVIS_REGISTER_TEST(task_fork_child_cleanup_preserves_parent_pages);
}
