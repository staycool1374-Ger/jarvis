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
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    Message msg{};
    msg.sender_id = cur->id;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(IPC::send(cur->id, msg));
    }
    JARVIS_ASSERT(cur->msg_queue->is_full());

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(sender);

    Scheduler::set_current(sender);
    bool ok = IPC::send(cur->id, msg);
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);
    JARVIS_ASSERT(cur->msg_queue->blocked_senders_head == sender);

    Scheduler::set_current(cur);
    cur->state = TaskState::TERMINATED;
    cur->exit_code = 0;

    Scheduler::reap_orphans();

    JARVIS_ASSERT(sender->state == TaskState::READY);
    JARVIS_ASSERT(cur->msg_queue->blocked_senders_head == nullptr);

    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(task_exit_frees_page_tables_correctly) {
    auto* tcb = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->page_table_ != 0);
    JARVIS_ASSERT(tcb->user_stack_ != 0);
    JARVIS_ASSERT(tcb->page_table_shared_ == false);

    uint64_t pml4 = tcb->page_table_;
    uint64_t ustack = tcb->user_stack_;

    tcb->cleanup();

    JARVIS_ASSERT(tcb->page_table_ == 0);
    JARVIS_ASSERT(tcb->user_stack_ == 0);
    JARVIS_ASSERT(tcb->stack_phys_ == 0);

    delete tcb;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(task_reparent_preserves_resources) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = parent->id;
    parent->add_child(child);
    Scheduler::add_task(child);

    JARVIS_ASSERT_EQ(1ULL, parent->num_children);

    child->state = TaskState::TERMINATED;
    child->exit_code = 42;

    Scheduler::reap_orphans();

    JARVIS_ASSERT(parent->num_children == 0);
    JARVIS_ASSERT(Scheduler::find_task(child->id) == nullptr);

    parent->cleanup();
    delete parent;
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
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* parent = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(parent != nullptr);
    parent->waiting_child_pid = static_cast<uint64_t>(-1);
    Scheduler::add_task(parent);

    auto* child = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(child != nullptr);
    child->parent_id = parent->id;
    child->state = TaskState::TERMINATED;
    child->exit_code = 0;
    Scheduler::add_task(child);

    uint64_t cnt = Scheduler::task_count();
    Scheduler::reap_orphans();
    JARVIS_ASSERT_EQ(cnt, Scheduler::task_count());

    parent->waiting_child_pid = 0;
    Scheduler::reap_orphans();
    JARVIS_ASSERT_EQ(cnt - 1, Scheduler::task_count());

    parent->cleanup();
    delete parent;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(elf_load_init_task_common_called) {
    elf::ELF64Header hdr{};
    elf::ELF64ProgramHeader phdr{};
    uint8_t data[4096];

    hdr.ident[0] = 0x7F; hdr.ident[1] = 'E'; hdr.ident[2] = 'L'; hdr.ident[3] = 'F';
    hdr.ident[4] = 2; hdr.ident[5] = 1; hdr.ident[6] = 1;
    for (int i = 7; i < 16; ++i) hdr.ident[i] = 0;
    hdr.type = elf::ET_EXEC; hdr.machine = 0x3E; hdr.version = 1;
    hdr.entry = 0x400000; hdr.phoff = sizeof(elf::ELF64Header);
    hdr.ehsize = sizeof(elf::ELF64Header); hdr.phentsize = sizeof(elf::ELF64ProgramHeader); hdr.phnum = 1;
    phdr.type = elf::PT_LOAD; phdr.flags = 5; phdr.offset = hdr.phoff + hdr.phentsize;
    phdr.vaddr = 0x400000; phdr.paddr = 0x400000; phdr.filesz = 0x1000; phdr.memsz = 0x1000; phdr.align = 0x1000;
    for (size_t i = 0; i < 0x1000; ++i) data[i] = 0x90;

    auto* tcb = elf::load(&hdr, data);
    JARVIS_ASSERT(tcb != nullptr);
    JARVIS_ASSERT(tcb->msg_queue != nullptr);
    JARVIS_ASSERT(tcb->notify != nullptr);
    JARVIS_ASSERT(tcb->event_group != nullptr);
    JARVIS_ASSERT(tcb->fd_table.get(0) != nullptr);
    JARVIS_ASSERT(tcb->cwd_vnode != nullptr);

    tcb->cleanup();
    delete tcb;
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
