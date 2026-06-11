// Runmode: kernel
// Testidea: BufferPool alloc/free/map/unmap/transfer/cleanup
// Depends: kernel::BufferPool, kernel::TaskControlBlock, kernel::Scheduler

#include <test.hpp>
#include <logger.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/syscall/syscall.hpp>
#include <constants.hpp>

using namespace kernel;

// -------------------------------------------------------------------
// All tests use create_user() so page_table_ is non-null.
// Tasks are not added to the scheduler (just created and cleaned up).
// -------------------------------------------------------------------

JARVIS_TEST(buffer_pool_basic_alloc_free) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x10000000;
    uint64_t handle = BufferPool::alloc(task, va);
    JARVIS_ASSERT(handle != 0);

    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    JARVIS_ASSERT(idx < BufferPool::MAX_BUFFERS);
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr != 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == va);

    JARVIS_ASSERT(BufferPool::free(task, handle));

    // After free, entry should be recycled
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr == 0);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_multiple_alloc) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t handles[5];
    uint64_t va = 0x20000000;
    for (int i = 0; i < 5; ++i) {
        handles[i] = BufferPool::alloc(task, va + i * arch::PAGE_SIZE);
        JARVIS_ASSERT(handles[i] != 0);
    }

    // Verify list has 5 entries
    int count = 0;
    int32_t idx = task->buf_list_head;
    while (idx != -1) {
        count++;
        idx = BufferPool::entries[idx].list_next;
    }
    JARVIS_ASSERT_EQ(5, count);

    // Free in reverse order
    for (int i = 4; i >= 0; --i) {
        JARVIS_ASSERT(BufferPool::free(task, handles[i]));
    }

    JARVIS_ASSERT_EQ(-1, task->buf_list_head);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_invalid_handle) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    // Handle 0 should always be invalid
    JARVIS_ASSERT_EQ(-1, BufferPool::validate(0));

    // Forged handle (index 0, gen 0xDEAD)
    uint64_t bad = (static_cast<uint64_t>(0xDEAD) << 32) | 0;
    JARVIS_ASSERT_EQ(-1, BufferPool::validate(bad));

    // Valid alloc then check bogus gen
    uint64_t va = 0x30000000;
    uint64_t good = BufferPool::alloc(task, va);
    JARVIS_ASSERT(good != 0);
    uint32_t real_idx = static_cast<uint32_t>(good & 0xFFFFFFFFULL);
    uint32_t real_gen = static_cast<uint32_t>(good >> 32);

    // Wrong generation
    uint64_t forged = (static_cast<uint64_t>(real_gen + 1) << 32) | real_idx;
    JARVIS_ASSERT_EQ(-1, BufferPool::validate(forged));

    // Index out of range
    uint64_t oob = (static_cast<uint64_t>(real_gen) << 32) | BufferPool::MAX_BUFFERS;
    JARVIS_ASSERT_EQ(-1, BufferPool::validate(oob));

    BufferPool::free(task, good);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_exhaustion) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    int alloc_count = 0;
    uint64_t va = 0x40000000;
    for (size_t i = 0; i < BufferPool::MAX_BUFFERS + 1; ++i) {
        uint64_t h = BufferPool::alloc(task, va + i * arch::PAGE_SIZE);
        if (h == 0) break;
        alloc_count++;
    }
    JARVIS_ASSERT_EQ(static_cast<int>(BufferPool::MAX_BUFFERS), alloc_count);

    // Free all
    int32_t idx = task->buf_list_head;
    while (idx != -1) {
        int32_t next = BufferPool::entries[idx].list_next;
        uint32_t gen = BufferPool::entries[idx].generation;
        uint64_t h = (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(idx);
        JARVIS_ASSERT(BufferPool::free(task, h));
        idx = next;
    }

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_double_free) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t handle = BufferPool::alloc(task, 0x50000000);
    JARVIS_ASSERT(handle != 0);

    JARVIS_ASSERT(BufferPool::free(task, handle));

    // Double free must fail
    JARVIS_ASSERT(!BufferPool::free(task, handle));

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_map_unmap) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x60000000;
    uint64_t handle = BufferPool::alloc(task, va);
    JARVIS_ASSERT(handle != 0);

    // Unmap
    JARVIS_ASSERT(BufferPool::unmap(task, handle));
    JARVIS_ASSERT_EQ(0ULL, BufferPool::entries[handle & 0xFFFFFFFF].mapped_va);

    // Free should still work (unmapped)
    JARVIS_ASSERT(BufferPool::free(task, handle));

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_transfer) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);

    uint64_t va = 0x80000000;
    uint64_t handle = BufferPool::alloc(sender, va);
    JARVIS_ASSERT(handle != 0);

    // Transfer -> receiver
    JARVIS_ASSERT(BufferPool::transfer(handle, sender, receiver));
    JARVIS_ASSERT_EQ(receiver->id, BufferPool::entries[handle & 0xFFFFFFFF].owner_task);
    JARVIS_ASSERT_EQ(0ULL, BufferPool::entries[handle & 0xFFFFFFFF].mapped_va);

    // Sender can no longer free it
    JARVIS_ASSERT(!BufferPool::free(sender, handle));

    // Receiver can map and free it
    uint64_t recv_va = 0x90000000;
    JARVIS_ASSERT(BufferPool::map(receiver, handle, recv_va));
    JARVIS_ASSERT(BufferPool::free(receiver, handle));

    sender->cleanup();
    delete sender;
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_unmap_all) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0xA0000000;
    uint64_t h1 = BufferPool::alloc(task, va);
    uint64_t h2 = BufferPool::alloc(task, va + arch::PAGE_SIZE);
    uint64_t h3 = BufferPool::alloc(task, va + 2 * arch::PAGE_SIZE);
    JARVIS_ASSERT(h1 != 0 && h2 != 0 && h3 != 0);

    BufferPool::unmap_all(task);
    JARVIS_ASSERT_EQ(-1, task->buf_list_head);

    JARVIS_ASSERT(BufferPool::entries[h1 & 0xFFFFFFFF].phys_addr == 0);
    JARVIS_ASSERT(BufferPool::entries[h2 & 0xFFFFFFFF].phys_addr == 0);
    JARVIS_ASSERT(BufferPool::entries[h3 & 0xFFFFFFFF].phys_addr == 0);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_syscall_dispatch) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);
    Scheduler::add_task(task);
    auto* original = Scheduler::current_task();
    Scheduler::set_current(task);

    uint64_t va = 0xB0000000;
    uint64_t handle = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_ALLOC), va, 0, 0, 0, nullptr);
    JARVIS_ASSERT(handle != 0);

    uint64_t result = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_FREE), handle, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, result);

    Scheduler::set_current(original);
    Scheduler::remove_task(task);
    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_ipc_transfer) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);
    Scheduler::add_task(sender);
    Scheduler::add_task(receiver);

    auto* original = Scheduler::current_task();

    // Sender allocs a buffer
    Scheduler::set_current(sender);
    uint64_t va = 0xC0000000;
    uint64_t handle = BufferPool::alloc(sender, va);
    JARVIS_ASSERT(handle != 0);

    // Send the buffer via IPC
    Message msg{};
    msg.buf_handle = handle;
    msg.type = 42;
    JARVIS_ASSERT(IPC::send(receiver->id, msg, 0));

    // Receiver gets the message
    Scheduler::set_current(receiver);
    Message recv_msg{};
    JARVIS_ASSERT(IPC::recv(recv_msg));
    JARVIS_ASSERT_EQ(42ULL, recv_msg.type);
    JARVIS_ASSERT_EQ(handle, recv_msg.buf_handle);

    // Sender can no longer free the buffer
    Scheduler::set_current(sender);
    JARVIS_ASSERT(!BufferPool::free(sender, handle));

    // Receiver can map and free it
    Scheduler::set_current(receiver);
    uint64_t recv_va = 0xD0000000;
    JARVIS_ASSERT(BufferPool::map(receiver, handle, recv_va));
    JARVIS_ASSERT(BufferPool::free(receiver, handle));

    Scheduler::set_current(original);
    Scheduler::remove_task(sender);
    Scheduler::remove_task(receiver);
    sender->cleanup();
    delete sender;
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

void register_buffer_pool_tests() {
    Logger::info("Registering BufferPool tests");

    JARVIS_REGISTER_TEST(buffer_pool_basic_alloc_free);
    JARVIS_REGISTER_TEST(buffer_pool_multiple_alloc);
    JARVIS_REGISTER_TEST(buffer_pool_invalid_handle);
    JARVIS_REGISTER_TEST(buffer_pool_exhaustion);
    JARVIS_REGISTER_TEST(buffer_pool_double_free);
    JARVIS_REGISTER_TEST(buffer_pool_map_unmap);
    JARVIS_REGISTER_TEST(buffer_pool_transfer);
    JARVIS_REGISTER_TEST(buffer_pool_unmap_all);
    JARVIS_REGISTER_TEST(buffer_pool_syscall_dispatch);
    JARVIS_REGISTER_TEST(buffer_pool_ipc_transfer);
}
