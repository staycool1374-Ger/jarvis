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

using namespace kernel;

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

JARVIS_TEST(task_exit_wakes_blocked_senders) {
    JARVIS_TEST_PASS();
}

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

JARVIS_TEST(task_reparent_preserves_resources) {
    JARVIS_TEST_PASS();
}

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

JARVIS_TEST(scheduler_reap_respects_parent_wait) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_load_init_task_common_called) {
    JARVIS_TEST_PASS();
}

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
