#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

static volatile uint64_t preempt_highpri_count = 0;
static volatile uint64_t preempt_lowpri_count = 0;
static volatile bool preempt_highpri_done = false;

static void preempt_highpri_task() {
    __atomic_add_fetch(&preempt_highpri_count, 1, __ATOMIC_RELAXED);
    preempt_highpri_done = true;
}

static void preempt_lowpri_syscall() {
    for (uint64_t i = 0; i < 1000000; ++i) {
        arch::pause();
        __atomic_add_fetch(&preempt_lowpri_count, 1, __ATOMIC_RELAXED);
        if (preempt_highpri_done) break;
    }
}

// Runmode: kernel
// Testidea: High-priority task preempts a low-priority task inside a long syscall.
// Input: Two tasks: low-pri (5) runs a long loop, high-pri (10) should preempt it.
// Expect: High-pri task runs at least once.
// Depends: Scheduler, context-switch atomics
JARVIS_TEST(preemption_during_syscall) {
    preempt_highpri_count = 0;
    preempt_lowpri_count = 0;
    preempt_highpri_done = false;

    auto* high = TaskControlBlock::create(preempt_highpri_task, 10, 1);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* low = TaskControlBlock::create(preempt_lowpri_syscall, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->user_data = const_cast<uint64_t*>(&preempt_highpri_count);
    Scheduler::add_task(*low);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);

    for (int tick = 0; tick < 10 && !preempt_highpri_count; ++tick) {
        low->state = TaskState::RUNNING;
        Scheduler::on_tick();
    }

    Scheduler::set_current(*original);

    JARVIS_ASSERT_FMT(preempt_highpri_count > 0,
                      "High-pri task ran %lu times (expected > 0)", preempt_highpri_count);

    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;

    JARVIS_TEST_PASS();
}

void register_preemption_under_syscall_tests() {
    Logger::info("Registering preemption-under-syscall tests");
    JARVIS_REGISTER_TEST(preemption_during_syscall);
}
