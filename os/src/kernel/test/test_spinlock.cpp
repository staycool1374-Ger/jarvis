#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/sync/spinlock_guard.hpp>

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

void register_spinlock_tests() {
    Logger::info("Registering SpinLock tests");
    JARVIS_REGISTER_TEST(spinlock_basic_lock_unlock);
    JARVIS_REGISTER_TEST(spinlock_guard_raii);
    JARVIS_REGISTER_TEST(spinlock_try_lock);
}
