#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/sync/spinlock_guard.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies basic SpinLock lock/unlock by protecting a shared flag.
// Input: SpinLock, two lock/unlock cycles with flag modification inside
// Expect: Flag values are as expected after each cycle
// Depends: kernel::sync::SpinLock
JARVIS_TEST(spinlock_basic_lock_unlock) {
    sync::SpinLock lock;
    int shared = 0;

    lock.lock();
    shared = 42;
    lock.unlock();
    JARVIS_ASSERT_EQ(42, shared);

    lock.lock();
    shared = 99;
    lock.unlock();
    JARVIS_ASSERT_EQ(99, shared);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies SpinLockGuard RAII guard locks and unlocks correctly.
// Input: SpinLockGuard protecting a shared flag modification
// Expect: Flag is written inside guard, readable outside
// Depends: kernel::sync::SpinLock, SpinLockGuard
JARVIS_TEST(spinlock_guard_raii) {
    sync::SpinLock lock;
    int shared = 0;

    {
        SpinLockGuard guard(lock);
        shared = 7;
    }
    JARVIS_ASSERT_EQ(7, shared);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that interrupts remain enabled inside a SpinLock critical
// section (i.e. no cli() is called).
// Input: SpinLock lock/unlock, read RFLAGS.IF before and after
// Expect: Interrupts are enabled during the entire SpinLock lifecycle
// Depends: kernel::sync::SpinLock, arch::interrupts_enabled
JARVIS_TEST(spinlock_no_irqguard) {
    sync::SpinLock lock;

    JARVIS_ASSERT(arch::interrupts_enabled());

    lock.lock();
    JARVIS_ASSERT(arch::interrupts_enabled());
    lock.unlock();

    JARVIS_ASSERT(arch::interrupts_enabled());

    {
        SpinLockGuard guard(lock);
        JARVIS_ASSERT(arch::interrupts_enabled());
    }

    JARVIS_ASSERT(arch::interrupts_enabled());

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies SpinLock try_lock succeeds when unlocked, fails when
// locked.
// Input: SpinLock, lock(), try_lock(), unlock(), try_lock()
// Expect: try_lock returns true when lock is free, false when held
// Depends: kernel::sync::SpinLock
JARVIS_TEST(spinlock_try_lock) {
    sync::SpinLock lock;

    JARVIS_ASSERT(lock.try_lock());
    JARVIS_ASSERT(!lock.try_lock());
    lock.unlock();
    JARVIS_ASSERT(lock.try_lock());
    lock.unlock();

    JARVIS_TEST_PASS();
}

struct SpinlockTestData {
    sync::SpinLock* lock;
    int* counter;
    int count;
};

// Runmode: kernel
// Testidea: Verifies two tasks contending for the same SpinLock both complete
// without data corruption. Each task yields after unlocking to let the other
// run.
// Input: SpinLock + shared counter; worker task locks and increments 50 times;
// main task locks and increments 50 times
// Expect: Final counter value is 100
// Depends: kernel::sync::SpinLock, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(spinlock_contention) {
    sync::SpinLock lock;
    int counter = 0;

    auto* worker = TaskControlBlock::create([]() {
        auto* d = reinterpret_cast<SpinlockTestData*>(
            Scheduler::current_task()->user_data);
        for (int i = 0; i < d->count; ++i) {
            d->lock->lock();
            ++(*d->counter);
            d->lock->unlock();
            Scheduler::reschedule();
        }
    }, 5, 10);
    JARVIS_ASSERT(worker != nullptr);

    SpinlockTestData wd = {&lock, &counter, 50};
    worker->user_data = &wd;
    Scheduler::add_task(*worker);

    auto* original = Scheduler::current_task();
    (void)original;
    Scheduler::set_current(*worker);

    for (int i = 0; i < 50; ++i) {
        lock.lock();
        ++counter;
        lock.unlock();
    }

    JARVIS_ASSERT_EQ(100, counter);

    Scheduler::remove_task(*worker);
    worker->cleanup();
    delete worker;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies SpinLock stress with 3 tasks performing lock/unlock cycles
// on a shared counter, yielding between iterations.
// Input: 3 worker tasks + SpinLock + shared counter
// Expect: Final counter value equals sum of all increments
// Depends: kernel::sync::SpinLock, kernel::TaskControlBlock, kernel::Scheduler
JARVIS_TEST(spinlock_stress_1000_ops) {
    sync::SpinLock lock;
    int counter = 0;
    const int OPS_PER_TASK = 500;

    auto* worker1 = TaskControlBlock::create([]() {
        auto* d = reinterpret_cast<SpinlockTestData*>(
            Scheduler::current_task()->user_data);
        for (int i = 0; i < d->count; ++i) {
            d->lock->lock();
            ++(*d->counter);
            d->lock->unlock();
            Scheduler::reschedule();
        }
    }, 5, 10);
    JARVIS_ASSERT(worker1 != nullptr);

    auto* worker2 = TaskControlBlock::create([]() {
        auto* d = reinterpret_cast<SpinlockTestData*>(
            Scheduler::current_task()->user_data);
        for (int i = 0; i < d->count; ++i) {
            d->lock->lock();
            ++(*d->counter);
            d->lock->unlock();
            Scheduler::reschedule();
        }
    }, 5, 10);
    JARVIS_ASSERT(worker2 != nullptr);

    SpinlockTestData wd1 = {&lock, &counter, OPS_PER_TASK};
    worker1->user_data = &wd1;
    SpinlockTestData wd2 = {&lock, &counter, OPS_PER_TASK};
    worker2->user_data = &wd2;

    Scheduler::add_task(*worker1);
    Scheduler::add_task(*worker2);

    for (int i = 0; i < OPS_PER_TASK; ++i) {
        lock.lock();
        ++counter;
        lock.unlock();
    }

    JARVIS_ASSERT_EQ(OPS_PER_TASK * 3, counter);

    Scheduler::remove_task(*worker2);
    worker2->cleanup();
    delete worker2;
    Scheduler::remove_task(*worker1);
    worker1->cleanup();
    delete worker1;
    JARVIS_TEST_PASS();
}

void register_spinlock_tests() {
    Logger::info("Registering SpinLock tests");
    JARVIS_REGISTER_TEST(spinlock_basic_lock_unlock);
    JARVIS_REGISTER_TEST(spinlock_guard_raii);
    JARVIS_REGISTER_TEST(spinlock_no_irqguard);
    JARVIS_REGISTER_TEST(spinlock_try_lock);
    JARVIS_REGISTER_TEST(spinlock_contention);
    JARVIS_REGISTER_TEST(spinlock_stress_1000_ops);
}
