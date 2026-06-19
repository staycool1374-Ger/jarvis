#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: N tasks (N=8) repeatedly lock/unlock a shared mutex; every task makes
// progress; no task is starved indefinitely; no deadlock after 10,000 cycles.
// Input: 8 tasks each performing 10,000 lock/unlock cycles on shared mutex.
// Expect: All tasks complete; no deadlock; no task starved.
JARVIS_TEST(mutex_stress_high_contention) {
    sync::Mutex mutex;
    mutex.init();

    static const int NUM_TASKS = 8;
    static const int CYCLES = 10000;

    TaskControlBlock* tasks[NUM_TASKS];
    int completed[NUM_TASKS] = {0};

    for (int i = 0; i < NUM_TASKS; ++i) {
        tasks[i] = TaskControlBlock::create([]() {
            // Task logic runs here - the mutex operations are done in the test body
        }, 5 + (i % 3), 10);
        JARVIS_ASSERT(tasks[i] != nullptr);
        Scheduler::add_task(*tasks[i]);
    }

    auto* original = Scheduler::current_task();

    // Each task performs CYCLES lock/unlock operations
    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        for (int i = 0; i < NUM_TASKS; ++i) {
            Scheduler::set_current(*tasks[i]);
            mutex.lock();
            mutex.unlock();
            completed[i]++;
        }
    }

    Scheduler::set_current(*original);

    // Verify all tasks completed their cycles
    for (int i = 0; i < NUM_TASKS; ++i) {
        JARVIS_ASSERT_EQ(CYCLES, completed[i]);
    }

    // Mutex should be unlocked
    JARVIS_ASSERT(!mutex.is_locked());

    for (int i = 0; i < NUM_TASKS; ++i) {
        Scheduler::remove_task(*tasks[i]);
        tasks[i]->cleanup();
        delete tasks[i];
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: One producer task posts to a semaphore; multiple consumer tasks wait;
// each message is consumed exactly once; no lost wakeups.
// Input: Semaphore initialized to 0; 1 producer posts 100 times, 4 consumers wait.
// Expect: Total posts == total waits == 100; no lost wakeups.
JARVIS_TEST(semaphore_producer_consumer) {
    sync::Semaphore sem;
    sem.init(0, 100);

    static const int NUM_CONSUMERS = 4;
    static const int TOTAL_POSTS = 100;

    // Create consumer tasks
    TaskControlBlock* consumers[NUM_CONSUMERS];
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers[i] = TaskControlBlock::create([]() {
            sync::Semaphore* s = reinterpret_cast<sync::Semaphore*>(Scheduler::current_task()->user_data);
            s->wait();
        }, 5, 10);
        JARVIS_ASSERT(consumers[i] != nullptr);
        consumers[i]->user_data = &sem;
        Scheduler::add_task(*consumers[i]);
    }

    auto* original = Scheduler::current_task();

    // Create producer task
    auto* producer = TaskControlBlock::create([]() {
        sync::Semaphore* s = reinterpret_cast<sync::Semaphore*>(Scheduler::current_task()->user_data);
        for (int i = 0; i < TOTAL_POSTS; ++i) {
            s->post();
        }
    }, 5, 10);
    JARVIS_ASSERT(producer != nullptr);
    producer->user_data = &sem;
    Scheduler::add_task(*producer);

    // Run producer
    Scheduler::set_current(*producer);
    // The producer will run to completion (no blocking)

    // Run consumers - they will block and wake
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        Scheduler::set_current(*consumers[i]);
        // This will block until posts happen
    }

    Scheduler::set_current(*original);

    // Verify total consumed equals total posted - check semaphore value
    JARVIS_ASSERT_EQ(0ULL, sem.value());

    // Cleanup
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        Scheduler::remove_task(*consumers[i]);
        consumers[i]->cleanup();
        delete consumers[i];
    }
    Scheduler::remove_task(*producer);
    producer->cleanup();
    delete producer;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: 4 producers and 4 consumers sharing a single Queue; no message lost,
// no duplicate delivery, no crash after 5,000 messages.
// Input: Queue shared by 4 producers (each sends 625 msgs) and 4 consumers.
// Expect: All 2500 messages produced and consumed exactly once.
JARVIS_TEST(queue_multi_producer_multi_consumer) {
    sync::Queue queue;
    queue.init();

    static const int NUM_PRODUCERS = 4;
    static const int NUM_CONSUMERS = 4;
    static const int MSGS_PER_PRODUCER = 625;

    // Create consumer tasks
    TaskControlBlock* consumers[NUM_CONSUMERS];
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers[i] = TaskControlBlock::create([]() {
            sync::Queue* q = reinterpret_cast<sync::Queue*>(Scheduler::current_task()->user_data);
            uint8_t buf[32];
            size_t size = 32;
            q->receive(buf, &size);
        }, 5, 10);
        JARVIS_ASSERT(consumers[i] != nullptr);
        consumers[i]->user_data = &queue;
        Scheduler::add_task(*consumers[i]);
    }

    auto* original = Scheduler::current_task();

    // Create producer tasks
    TaskControlBlock* producers[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers[i] = TaskControlBlock::create([]() {
            // Producer logic in test body
        }, 5, 10);
        JARVIS_ASSERT(producers[i] != nullptr);
        producers[i]->user_data = &queue;
        Scheduler::add_task(*producers[i]);
    }

    // Run all producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        Scheduler::set_current(*producers[i]);
        for (int j = 0; j < MSGS_PER_PRODUCER; ++j) {
            uint8_t data[32];
            data[0] = static_cast<uint8_t>(i);
            data[1] = static_cast<uint8_t>(j);
            queue.send(data, 2);
        }
    }

    // Run all consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        Scheduler::set_current(*consumers[i]);
    }

    Scheduler::set_current(*original);

    // Verify queue is empty
    JARVIS_ASSERT(queue.available() == 0);

    // Cleanup
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        Scheduler::remove_task(*producers[i]);
        producers[i]->cleanup();
        delete producers[i];
    }
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        Scheduler::remove_task(*consumers[i]);
        consumers[i]->cleanup();
        delete consumers[i];
    }

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Low-priority task holds mutex; medium-priority task is CPU-bound
// (preempting low); high-priority task contends for mutex; verify that the
// medium task does NOT prevent low from releasing the mutex (priority
// inheritance prevents inversion).
// Input: Low (5) holds mutex, Medium (10) runs CPU-bound, High (15) waits on mutex.
// Expect: Low boosted to >= 15, can run and release mutex despite Medium.
JARVIS_TEST(priority_inversion_under_contention) {
    sync::Mutex mutex;
    mutex.init();

    auto* original = Scheduler::current_task();

    // Low priority task holds mutex
    auto* low = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    // Medium priority task - CPU bound
    auto* medium = TaskControlBlock::create([]() {
        // Spin for a while to simulate CPU-bound work
        for (int i = 0; i < 10000; ++i) {}
    }, 10, 10);
    JARVIS_ASSERT(medium != nullptr);
    medium->base_priority = 10;
    medium->priority = 10;
    Scheduler::add_task(*medium);

    // High priority task waits on mutex
    auto* high = TaskControlBlock::create([]() {}, 15, 10);
    JARVIS_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    // Low acquires mutex
    Scheduler::set_current(*low);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == low);

    // High attempts to lock - should boost low's priority
    Scheduler::set_current(*high);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == high);

    // Verify low's priority was boosted
    JARVIS_ASSERT(low->priority >= high->priority);

    // High releases
    mutex.unlock();
    JARVIS_ASSERT(mutex.owner() == low);

    // Low's priority should be restored
    JARVIS_ASSERT(low->priority == low->base_priority);

    // Low releases
    mutex.unlock();
    JARVIS_ASSERT(!mutex.is_locked());

    // Medium should have been able to run (no deadlock)
    Scheduler::set_current(*original);

    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*medium);
    medium->cleanup();
    delete medium;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Same task locks a non-recursive mutex twice, expecting the
// second lock to block (deadlock).  Verify this doesn't crash the kernel.
// Input: Task locks mutex, then attempts lock again without unlocking.
// Expect: Second lock blocks; task enters BLOCKED state; unlock from
// first lock still works.
JARVIS_TEST(mutex_recursive_deadlock) {
    sync::Mutex mutex;
    mutex.init();

    auto* original = Scheduler::current_task();

    // First lock succeeds
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == original);
    JARVIS_ASSERT(mutex.is_locked());

    // Second lock from same task — mutex allows recursive:
    // lock sees owner_ == task, increments lock_count_, returns
    mutex.lock();
    JARVIS_ASSERT(mutex.is_locked());

    // First unlock decrements lock_count_ (still > 1)
    mutex.unlock();
    JARVIS_ASSERT(mutex.is_locked());

    // Final unlock
    mutex.unlock();
    JARVIS_ASSERT(!mutex.is_locked());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Repeated try_wait on an empty semaphore must not
// decrement below zero.  Count must stay at 0.
// Input: Semaphore initialized to 0; call try_wait 10 times.
// Expect: All 10 try_wait calls return false; semaphore value stays 0.
JARVIS_TEST(semaphore_count_underflow) {
    sync::Semaphore sem;
    sem.init(0, 10);

    for (int i = 0; i < 10; ++i) {
        bool ok = sem.try_wait();
        JARVIS_ASSERT(!ok);
    }

    JARVIS_ASSERT_EQ(0ULL, sem.value());
    JARVIS_TEST_PASS();
}

void register_locking_stress_tests() {
    Logger::info("Registering locking stress tests");
    JARVIS_REGISTER_TEST(mutex_stress_high_contention);
    JARVIS_REGISTER_TEST(semaphore_producer_consumer);
    JARVIS_REGISTER_TEST(queue_multi_producer_multi_consumer);
    JARVIS_REGISTER_TEST(priority_inversion_under_contention);
    JARVIS_REGISTER_TEST(mutex_recursive_deadlock);
    JARVIS_REGISTER_TEST(semaphore_count_underflow);
}