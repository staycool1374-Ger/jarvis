#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/timer.hpp>

using namespace kernel;

JARVIS_TEST(idle_task_created_at_boot) {
    uint64_t count = Scheduler::task_count();
    bool found = false;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            found = true;
            break;
        }
    }
    JARVIS_ASSERT(found);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_runs_in_ring3) {
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            JARVIS_ASSERT(t->page_table_ != VMM::get_kernel_pml4());
            JARVIS_ASSERT(t->kernel_stack != nullptr);
            JARVIS_TEST_PASS();
            return;
        }
    }
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_priority_zero) {
    uint64_t count = Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            JARVIS_ASSERT_EQ(0ULL, t->priority);
            JARVIS_ASSERT_EQ(0ULL, t->base_priority);
            JARVIS_TEST_PASS();
            return;
        }
    }
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_calls_pause_syscall) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t count = Scheduler::task_count();
    TaskControlBlock* idle = nullptr;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            idle = t;
            break;
        }
    }
    JARVIS_ASSERT(idle != nullptr);

    Scheduler::set_current(idle);
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::PAUSE), 0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    Scheduler::set_current(cur);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_yields_to_higher_priority) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    auto* worker = TaskControlBlock::create([]() {}, 10, 20);
    JARVIS_ASSERT(worker != nullptr);
    Scheduler::add_task(worker);

    uint64_t count = Scheduler::task_count();
    TaskControlBlock* idle = nullptr;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            idle = t;
            break;
        }
    }
    JARVIS_ASSERT(idle != nullptr);

    Scheduler::set_current(idle);
    Scheduler::reschedule();

    auto* after = Scheduler::current_task();
    JARVIS_ASSERT(after != idle);
    JARVIS_ASSERT(after->priority > 0);

    Scheduler::set_current(cur);
    worker->cleanup();
    delete worker;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(kernel_hlt_idle_still_exists) {
    JARVIS_ASSERT(Scheduler::idle_task_ != nullptr);
    JARVIS_ASSERT(Scheduler::idle_task_->kernel_stack != nullptr);
    JARVIS_ASSERT(Scheduler::idle_task_->user_stack_ == 0);
    JARVIS_ASSERT_EQ(Scheduler::idle_task_, Scheduler::task_at(0));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(idle_task_restartable_on_crash) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t count = Scheduler::task_count();
    TaskControlBlock* idle = nullptr;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            idle = t;
            break;
        }
    }
    JARVIS_ASSERT(idle != nullptr);

    idle->state = TaskState::TERMINATED;
    idle->exit_code = 1;

    Scheduler::reap_orphans();

    count = Scheduler::task_count();
    bool found = false;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            found = true;
            break;
        }
    }
    JARVIS_ASSERT(found);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(multiple_idle_tasks_prevented) {
    uint64_t count = Scheduler::task_count();
    int idle_count = 0;
    for (uint64_t i = 0; i < count; ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t->priority == 0 && t->user_stack_ != 0 && t != Scheduler::idle_task_) {
            idle_count++;
        }
    }
    JARVIS_ASSERT_EQ(1, idle_count);
    JARVIS_TEST_PASS();
}

void register_idle_task_tests() {
    Logger::info("Registering idle task tests");
    JARVIS_REGISTER_TEST(idle_task_created_at_boot);
    JARVIS_REGISTER_TEST(idle_task_runs_in_ring3);
    JARVIS_REGISTER_TEST(idle_task_priority_zero);
    JARVIS_REGISTER_TEST(idle_task_calls_pause_syscall);
    JARVIS_REGISTER_TEST(idle_task_yields_to_higher_priority);
    JARVIS_REGISTER_TEST(kernel_hlt_idle_still_exists);
    JARVIS_REGISTER_TEST(idle_task_restartable_on_crash);
    JARVIS_REGISTER_TEST(multiple_idle_tasks_prevented);
}
