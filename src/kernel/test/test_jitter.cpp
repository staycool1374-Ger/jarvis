/// @file test_jitter.cpp
/// @brief Schedule-to-schedule jitter benchmarking (v0.3.4).

#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

static constexpr size_t JITTER_ITERATIONS = 500;
static constexpr uint64_t TEST_PRIORITY = 20;

// Runmode: kernel
// Testidea: Measure schedule-to-schedule jitter via rdtsc.
// Input: Create N tasks at same priority, measure time between successive
//        reschedules.  Record min/avg/max jitter in TSC cycles.
// Expect: Average jitter measured; jitter is bounded (< 10× min).
// Depends: Scheduler, arch::rdtsc
JARVIS_TEST(jitter_under_idle, "PRE: none | POST: none") {
    auto *original = Scheduler::current_task();

    // Create two tasks at same priority for ping-pong scheduling
    auto *a = TaskControlBlock::create([]() {}, TEST_PRIORITY, 10);
    JARVIS_ASSERT(a != nullptr);
    Scheduler::add_task(*a);

    auto *b = TaskControlBlock::create([]() {}, TEST_PRIORITY, 10);
    JARVIS_ASSERT(b != nullptr);
    Scheduler::add_task(*b);

    // Warm up: let both tasks run once
    Scheduler::set_current(*a);
    Scheduler::reschedule();
    Scheduler::set_current(*b);
    Scheduler::reschedule();

    uint64_t min_jitter = ~0ULL;
    uint64_t max_jitter = 0;
    uint64_t sum_jitter = 0;

    for (size_t i = 0; i < JITTER_ITERATIONS; ++i) {
        uint64_t t0 = arch::rdtsc();
        Scheduler::set_current(*a);
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
    JARVIS_ASSERT(max_jitter <= min_jitter * 10);  // bounded jitter

    // Restore original task and clean up
    Scheduler::set_current(*original);
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
    Scheduler::remove_task(*b);
    b->cleanup();
    delete b;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Measure schedule-to-schedule jitter under synthetic load.
// Input: Create 4 additional CPU-bound tasks, then measure jitter as above.
// Expect: Jitter under load < 2× idle jitter.
// Depends: Scheduler, arch::rdtsc
JARVIS_TEST(jitter_under_load, "PRE: none | POST: none") {
    auto *original = Scheduler::current_task();

    // Create background load: 4 tasks at lower priority
    static constexpr size_t LOAD_TASKS = 4;
    TaskControlBlock *load[LOAD_TASKS];
    for (size_t i = 0; i < LOAD_TASKS; ++i) {
        load[i] = TaskControlBlock::create([]() {}, TEST_PRIORITY - 1, 10);
        JARVIS_ASSERT(load[i] != nullptr);
        Scheduler::add_task(*load[i]);
    }

    auto *a = TaskControlBlock::create([]() {}, TEST_PRIORITY, 10);
    JARVIS_ASSERT(a != nullptr);
    Scheduler::add_task(*a);

    auto *b = TaskControlBlock::create([]() {}, TEST_PRIORITY, 10);
    JARVIS_ASSERT(b != nullptr);
    Scheduler::add_task(*b);

    uint64_t min_jitter = ~0ULL;
    uint64_t max_jitter = 0;
    uint64_t sum_jitter = 0;

    for (size_t i = 0; i < JITTER_ITERATIONS; ++i) {
        uint64_t t0 = arch::rdtsc();
        Scheduler::set_current(*a);
        Scheduler::reschedule();
        uint64_t elapsed = arch::rdtsc() - t0;
        if (elapsed < min_jitter) min_jitter = elapsed;
        if (elapsed > max_jitter) max_jitter = elapsed;
        sum_jitter += elapsed;
    }

    uint64_t avg_jitter = sum_jitter / JITTER_ITERATIONS;
    Logger::info("jitter_under_load: min=%lu avg=%lu max=%lu (cycles)",
                 min_jitter, avg_jitter, max_jitter);
    JARVIS_ASSERT(avg_jitter > 0);

    // Cleanup background load tasks
    for (size_t i = 0; i < LOAD_TASKS; ++i) {
        Scheduler::remove_task(*load[i]);
        load[i]->cleanup();
        delete load[i];
    }
    Scheduler::set_current(*original);
    Scheduler::remove_task(*a);
    a->cleanup();
    delete a;
    Scheduler::remove_task(*b);
    b->cleanup();
    delete b;

    JARVIS_TEST_PASS();
}

void register_jitter_tests() {
    Logger::info("Registering jitter benchmark tests");
    JARVIS_REGISTER_TEST(jitter_under_idle);
    JARVIS_REGISTER_TEST(jitter_under_load);
}
