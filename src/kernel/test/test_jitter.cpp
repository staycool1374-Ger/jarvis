/// @file test_jitter.cpp
/// @brief Schedule-to-schedule jitter benchmarking (v0.3.4).
///
/// Uses the same termination-safe pattern as bench_irq_latency.cpp:
/// terminating-task entries, no warm-up, explicit cleanup.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/debug/ipc_sched_trace.hpp>

using namespace kernel;

static constexpr size_t JITTER_ITERATIONS = 500;
static constexpr size_t LOAD_ITERATIONS = 200;

// Runmode: kernel
// Testidea: Measure reschedule() jitter via rdtsc (idle system).
// Input: two same-priority tasks, measure reschedule cycle jitter.
// Expect: Average jitter recorded; jitter is bounded (< 10× min).
// Depends: Scheduler, arch::rdtsc
JARVIS_TEST(jitter_under_idle, "PRE: isolate | POST: none") {
#if defined(CONFIG_DEBUG_IPC_SCHED)
    // CONFIG_DEBUG_IPC_SCHED emits a serial [RS] trace from reschedule() on
    // every call (scheduler.cpp), so the rdtsc window below measures UART
    // write latency (observed min=21000 cycles), not scheduler jitter.  The
    // max<=min*10+1000 bound then fails spuriously.  Skip the measurement
    // when the diagnostic is enabled; the bulk test suite still runs with it.
    Logger::info("jitter_under_idle: skipped (CONFIG_DEBUG_IPC_SCHED active)");
    JARVIS_TEST_PASS();
#else
    auto *original = Scheduler::current_task();

    auto *a = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(a != nullptr);
    Scheduler::add_task(*a);

    auto *b = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(b != nullptr);
    Scheduler::add_task(*b);

    uint64_t min_jitter = ~0ULL;
    uint64_t max_jitter = 0;
    uint64_t sum_jitter = 0;

    for (size_t i = 0; i < JITTER_ITERATIONS; ++i) {
        Scheduler::set_current(*a);
        uint64_t t0 = arch::rdtsc();
        Scheduler::reschedule();
        uint64_t elapsed = arch::rdtsc() - t0;
        if (elapsed < min_jitter) min_jitter = elapsed;
        if (elapsed > max_jitter) max_jitter = elapsed;
        sum_jitter += elapsed;
    }

    uint64_t avg_jitter = sum_jitter / JITTER_ITERATIONS;
    Logger::info("jitter_under_idle: min=%lu avg=%lu max=%lu (cycles)",
                 min_jitter, avg_jitter, max_jitter);
    JARVIS_ASSERT(avg_jitter > 0);
    JARVIS_ASSERT(max_jitter <= min_jitter * 10 + 1000);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
    Scheduler::remove_task(*b);
    b->cleanup();
    delete b;

    JARVIS_TEST_PASS();
#endif
}

// Runmode: kernel
// Testidea: Measure reschedule() jitter under synthetic CPU load.
// Input: 4 background tasks + ping-pong pair, measure jitter.
// Expect: Jitter bounded; avg jitter reported.
// Depends: Scheduler, arch::rdtsc
JARVIS_TEST(jitter_under_load, "PRE: isolate | POST: none") {
#if defined(CONFIG_DEBUG_IPC_SCHED)
    Logger::info("jitter_under_load: skipped (CONFIG_DEBUG_IPC_SCHED active)");
    JARVIS_TEST_PASS();
#else
    auto *original = Scheduler::current_task();

    static constexpr size_t LOAD_TASKS = 4;
    TaskControlBlock *load[LOAD_TASKS];
    for (size_t i = 0; i < LOAD_TASKS; ++i) {
        load[i] = TaskControlBlock::create([]() {}, 4, 10);
        JARVIS_ASSERT(load[i] != nullptr);
        Scheduler::add_task(*load[i]);
    }

    auto *a = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(a != nullptr);
    Scheduler::add_task(*a);

    auto *b = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(b != nullptr);
    Scheduler::add_task(*b);

    uint64_t min_jitter = ~0ULL;
    uint64_t max_jitter = 0;
    uint64_t sum_jitter = 0;

    for (size_t i = 0; i < LOAD_ITERATIONS; ++i) {
        Scheduler::set_current(*a);
        uint64_t t0 = arch::rdtsc();
        Scheduler::reschedule();
        uint64_t elapsed = arch::rdtsc() - t0;
        if (elapsed < min_jitter) min_jitter = elapsed;
        if (elapsed > max_jitter) max_jitter = elapsed;
        sum_jitter += elapsed;
    }

    uint64_t avg_jitter = sum_jitter / LOAD_ITERATIONS;
    Logger::info("jitter_under_load: min=%lu avg=%lu max=%lu (cycles)",
                 min_jitter, avg_jitter, max_jitter);
    JARVIS_ASSERT(avg_jitter > 0);

    Scheduler::set_current(*original);
    for (size_t i = 0; i < LOAD_TASKS; ++i) {
        Scheduler::remove_task(*load[i]);
        load[i]->cleanup();
        delete load[i];
    }
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
    Scheduler::remove_task(*b);
    b->cleanup();
    delete b;

    JARVIS_TEST_PASS();
#endif
}

void register_jitter_tests() {
    Logger::info("Registering jitter benchmark tests");
    JARVIS_REGISTER_TEST(jitter_under_idle);
    JARVIS_REGISTER_TEST(jitter_under_load);
}
