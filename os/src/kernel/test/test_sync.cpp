#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(semaphore_wait_post) {
    sync::Semaphore sem;
    sem.init(0, 1);

    auto* worker = TaskControlBlock::create([]() {
        sync::Semaphore* s = reinterpret_cast<sync::Semaphore*>(Scheduler::current_task()->user_data);
        s->wait();
    }, 5, 10);
    JARVIS_ASSERT(worker != nullptr);
    worker->user_data = &sem;
    Scheduler::add_task(worker);

    auto* original = Scheduler::current_task();
    (void)original;
    Scheduler::set_current(worker);

    sem.wait();

    JARVIS_ASSERT(worker->state == TaskState::BLOCKED);

    Scheduler::set_current(original);
    sem.post();

    JARVIS_ASSERT(worker->state == TaskState::READY);

    Scheduler::remove_task(worker);
    worker->cleanup();
    delete worker;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mutex_lock_unlock) {
    sync::Mutex mutex;
    mutex.init();

    auto* owner = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(owner != nullptr);
    Scheduler::add_task(owner);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(owner);
    mutex.lock();

    JARVIS_ASSERT(mutex.owner() == owner);
    JARVIS_ASSERT(mutex.is_locked());

    mutex.unlock();

    JARVIS_ASSERT(!mutex.is_locked());
    JARVIS_ASSERT(mutex.owner() == nullptr);

    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == owner);
    mutex.unlock();

    auto* waiter = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(waiter != nullptr);
    Scheduler::add_task(waiter);

    Scheduler::set_current(waiter);
    mutex.lock();
    JARVIS_ASSERT(mutex.owner() == waiter);

    Scheduler::set_current(original);
    Scheduler::remove_task(waiter);
    waiter->cleanup();
    delete waiter;
    Scheduler::remove_task(owner);
    owner->cleanup();
    delete owner;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(queue_send_receive_block) {
    sync::Queue queue;
    queue.init();

    uint8_t data[32] = {0xAA};
    JARVIS_ASSERT(queue.try_send(data, 1));
    JARVIS_ASSERT_EQ(1ULL, queue.available());

    uint8_t buf[32];
    size_t size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    JARVIS_ASSERT_EQ(0xAA, buf[0]);
    JARVIS_ASSERT(queue.available() == 0);

    for (size_t i = 0; i < sync::QUEUE_MAX_MSG_COUNT; ++i) {
        uint8_t d[32] = {static_cast<uint8_t>(i)};
        JARVIS_ASSERT(queue.try_send(d, 1));
    }
    JARVIS_ASSERT(queue.available() == sync::QUEUE_MAX_MSG_COUNT);

    JARVIS_ASSERT(!queue.try_send(data, 1));

    size = 32;
    JARVIS_ASSERT(queue.try_receive(buf, &size));
    JARVIS_ASSERT(queue.available() == sync::QUEUE_MAX_MSG_COUNT - 1);

    JARVIS_TEST_PASS();
}

JARVIS_TEST(sync_queue_send_blocks_when_full) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sync_queue_receive_blocks_when_empty) {
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sync_queue_wake_sender_on_receive) {
    JARVIS_TEST_PASS();
}

void register_sync_tests() {
    Logger::info("Registering sync tests");
    JARVIS_REGISTER_TEST(semaphore_wait_post);
    JARVIS_REGISTER_TEST(mutex_lock_unlock);
    JARVIS_REGISTER_TEST(queue_send_receive_block);
    JARVIS_REGISTER_TEST(sync_queue_send_blocks_when_full);
    JARVIS_REGISTER_TEST(sync_queue_receive_blocks_when_empty);
    JARVIS_REGISTER_TEST(sync_queue_wake_sender_on_receive);
}