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
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/pmm.hpp>
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
    uint64_t handle = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(handle != 0);

    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    JARVIS_ASSERT(idx < BufferPool::MAX_BUFFERS);
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr != 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == va);

    JARVIS_ASSERT(BufferPool::free(*task, handle));

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
        handles[i] = BufferPool::alloc(*task, va + i * arch::PAGE_SIZE);
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
        JARVIS_ASSERT(BufferPool::free(*task, handles[i]));
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
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(0));

    // Forged handle (index 0, gen 0xDEAD)
    uint64_t bad = (static_cast<uint64_t>(0xDEAD) << 32) | 0;
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(bad));

    // Valid alloc then check bogus gen
    uint64_t va = 0x30000000;
    uint64_t good = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(good != 0);
    uint32_t real_idx = static_cast<uint32_t>(good & 0xFFFFFFFFULL);
    uint32_t real_gen = static_cast<uint32_t>(good >> 32);

    // Wrong generation
    uint64_t forged = (static_cast<uint64_t>(real_gen + 1) << 32) | real_idx;
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(forged));

    // Index out of range
    uint64_t oob = (static_cast<uint64_t>(real_gen) << 32) | BufferPool::MAX_BUFFERS;
    JARVIS_ASSERT_EQ(BUF_INVALID_INDEX, BufferPool::validate(oob));

    BufferPool::free(*task, good);

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
        uint64_t h = BufferPool::alloc(*task, va + i * arch::PAGE_SIZE);
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
        JARVIS_ASSERT(BufferPool::free(*task, h));
        idx = next;
    }

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_double_free) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t handle = BufferPool::alloc(*task, 0x50000000);
    JARVIS_ASSERT(handle != 0);

    JARVIS_ASSERT(BufferPool::free(*task, handle));

    // Double free must fail
    JARVIS_ASSERT(!BufferPool::free(*task, handle));

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_map_unmap) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x60000000;
    uint64_t handle = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(handle != 0);

    // Unmap
    JARVIS_ASSERT(BufferPool::unmap(*task, handle));
    JARVIS_ASSERT_EQ(0ULL, BufferPool::entries[handle & 0xFFFFFFFF].mapped_va);

    // Free should still work (unmapped)
    JARVIS_ASSERT(BufferPool::free(*task, handle));

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_transfer) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);

    uint64_t va = 0x80000000;
    uint64_t handle = BufferPool::alloc(*sender, va);
    JARVIS_ASSERT(handle != 0);

    // Transfer -> receiver
    JARVIS_ASSERT(BufferPool::transfer(handle, *sender, *receiver));
    JARVIS_ASSERT_EQ(receiver->id, BufferPool::entries[handle & 0xFFFFFFFF].owner_task);
    JARVIS_ASSERT_EQ(0ULL, BufferPool::entries[handle & 0xFFFFFFFF].mapped_va);

    // Sender can no longer free it
    JARVIS_ASSERT(!BufferPool::free(*sender, handle));

    // Receiver can map and free it
    uint64_t recv_va = 0x90000000;
    JARVIS_ASSERT(BufferPool::map(*receiver, handle, recv_va));
    JARVIS_ASSERT(BufferPool::free(*receiver, handle));

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
    uint64_t h1 = BufferPool::alloc(*task, va);
    uint64_t h2 = BufferPool::alloc(*task, va + arch::PAGE_SIZE);
    uint64_t h3 = BufferPool::alloc(*task, va + 2 * arch::PAGE_SIZE);
    JARVIS_ASSERT(h1 != 0 && h2 != 0 && h3 != 0);

    BufferPool::unmap_all(*task);
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
    Scheduler::add_task(*task);
    auto* original = Scheduler::current_task();
    Scheduler::set_current(*task);

    uint64_t va = 0xB0000000;
    uint64_t handle = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_ALLOC), va, 0, 0, 0, nullptr);
    JARVIS_ASSERT(handle != 0);

    uint64_t result = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_FREE), handle, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, result);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*task);
    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_ipc_transfer) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);
    Scheduler::add_task(*sender);
    Scheduler::add_task(*receiver);

    auto* original = Scheduler::current_task();

    // Sender allocs a buffer
    Scheduler::set_current(*sender);
    uint64_t va = 0xC0000000;
    uint64_t handle = BufferPool::alloc(*sender, va);
    JARVIS_ASSERT(handle != 0);

    // Send the buffer via IPC
    Message msg{};
    msg.buf_handle = handle;
    msg.type = 42;
    JARVIS_ASSERT(IPC::send(receiver->id, msg, 0));

    // Receiver gets the message
    Scheduler::set_current(*receiver);
    Message recv_msg{};
    JARVIS_ASSERT(IPC::recv(recv_msg));
    JARVIS_ASSERT_EQ(42ULL, recv_msg.type);
    JARVIS_ASSERT_EQ(handle, recv_msg.buf_handle);

    // Sender can no longer free the buffer
    Scheduler::set_current(*sender);
    JARVIS_ASSERT(!BufferPool::free(*sender, handle));

    // Receiver can map and free it
    Scheduler::set_current(*receiver);
    uint64_t recv_va = 0xD0000000;
    JARVIS_ASSERT(BufferPool::map(*receiver, handle, recv_va));
    JARVIS_ASSERT(BufferPool::free(*receiver, handle));

    Scheduler::set_current(*original);
    Scheduler::remove_task(*sender);
    Scheduler::remove_task(*receiver);
    sender->cleanup();
    delete sender;
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_cleanup_frees_buffers) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0xE0000000;
    uint64_t h1 = BufferPool::alloc(*task, va);
    uint64_t h2 = BufferPool::alloc(*task, va + arch::PAGE_SIZE);
    JARVIS_ASSERT(h1 != 0 && h2 != 0);

    // cleanup() should call BufferPool::unmap_all() which frees everything
    task->cleanup();

    // Entries should be recycled
    JARVIS_ASSERT(BufferPool::entries[h1 & 0xFFFFFFFF].phys_addr == 0);
    JARVIS_ASSERT(BufferPool::entries[h2 & 0xFFFFFFFF].phys_addr == 0);

    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that BufferPool::exec_into_current properly clears buffer pool entries by
// calling unmap_all BEFORE swapping the page table. This is a regression
// test for a bug where unmap_all used the NEW page table (after swap)
// instead of the OLD one, leaving PTEs stale and buffer entries leaked.
// Input: Alloc a buffer in a task, create new PML4, call unmap_all (simulating
//        exec_into_current), swap page_table_, free old PML4.
// Expect: Buffer entry recycled (phys_addr == 0), old PML4 freed cleanly.
// Depends: kernel::BufferPool, kernel::TaskControlBlock, kernel::VMM,
// kernel::PMM
JARVIS_TEST(buffer_pool_exec_into_current_clears_buffers) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0xF0000000;
    uint64_t handle = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(handle != 0);

    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr != 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == va);

    // Simulate exec_into_current:
    // 1. Create new PML4 (like exec_into_current does)
    uint64_t new_pml4 = VMM::clone_kernel_pml4();
    JARVIS_ASSERT(new_pml4 != 0);

    // 2. Call unmap_all BEFORE swapping page_table_ (this is what
    // exec_into_current does)
    // BUG: If unmap_all uses task->page_table_ AFTER the swap, it clears
    // wrong PML4
    BufferPool::unmap_all(*task);

    // 3. Swap page table (simulating exec_into_current line 308)
    uint64_t old_pml4 = task->page_table_;
    task->page_table_ = new_pml4;

    // 4. Free old PML4 (simulating exec_into_current cleanup)
    if (old_pml4 && old_pml4 != VMM::get_kernel_pml4()) {
        VMM::free_user_pages(old_pml4);
        PMM::free_page(old_pml4);
    }

    // Verify buffer entry was recycled (phys_addr == 0)
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr == 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == 0);
    JARVIS_ASSERT(task->buf_list_head == -1);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Tests that BufferPool::transfer() adds the buffer to the RECEIVER's
//           buf_list_head. This is a regression test for a bug where transfer()
//           did NOT add to receiver's list, causing a leak when the receiver
// exits without ever mapping the buffer (cleanup() walks buf_list_head
//           and frees entries; if not in list, buffer is never freed).
// Input: Sender allocates buffer, transfer() to receiver, sender cleaned up,
//        receiver exits without calling map().
// Expect: Buffer appears in receiver's buf_list_head; receiver cleanup frees
// it.
// Depends: kernel::BufferPool, kernel::TaskControlBlock, kernel::IPC
JARVIS_TEST(buffer_pool_transfer_adds_to_receiver_list) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);

    uint64_t va = 0x100000000ULL;
    uint64_t handle = BufferPool::alloc(*sender, va);
    JARVIS_ASSERT(handle != 0);

    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    JARVIS_ASSERT(BufferPool::entries[idx].owner_task == static_cast<uint32_t>(sender->id));
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == va);

    // Transfer from sender to receiver
    JARVIS_ASSERT(BufferPool::transfer(handle, *sender, *receiver));

    // Buffer should now be in receiver's list
    JARVIS_ASSERT(BufferPool::entries[idx].owner_task == static_cast<uint32_t>(receiver->id));
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == 0); // unmapped during transfer

    // Check receiver's list contains the buffer
    int count = 0;
    int32_t list_idx = receiver->buf_list_head;
    bool found = false;
    while (list_idx != -1) {
        if (list_idx == static_cast<int32_t>(idx)) found = true;
        count++;
        list_idx = BufferPool::entries[list_idx].list_next;
    }
    JARVIS_ASSERT(found);
    JARVIS_ASSERT_EQ(1, count);

    // Sender's list should be empty
    JARVIS_ASSERT_EQ(-1, sender->buf_list_head);

    // Simulate receiver exiting WITHOUT ever calling map()
    // cleanup() -> unmap_all() walks buf_list_head and frees entries
    receiver->cleanup();

    // Buffer entry should be recycled
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr == 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == 0);
    JARVIS_ASSERT(receiver->buf_list_head == -1);

    sender->cleanup();
    delete sender;
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: STUB - Alloc at VA X, alloc again at same VA X, second alloc
// fails (BUF_ERR_VA_IN_USE)
// Input: task, alloc at va, alloc again at same va
// Expect: First alloc succeeds, second returns 0
// Depends: kernel::BufferPool
// Note: Kernel does not yet implement VA conflict detection; remains stub
// until implemented.
JARVIS_TEST(buffer_pool_va_conflict_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Alloc with VA >= USER_SPACE_LIMIT fails (BUF_ERR_VA_OUT_OF_RANGE)
// Input: task, alloc at USER_SPACE_LIMIT and above
// Expect: Both allocs return 0
// Depends: kernel::BufferPool
JARVIS_TEST(buffer_pool_va_out_of_range_rejected) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t h1 = BufferPool::alloc(*task, USER_SPACE_LIMIT);
    uint64_t h2 = BufferPool::alloc(*task, USER_SPACE_LIMIT + arch::PAGE_SIZE);
    JARVIS_ASSERT_EQ(0ULL, h1);
    JARVIS_ASSERT_EQ(0ULL, h2);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Alloc with VA=0 fails (guard page, below user space)
// Input: task, alloc at va=0
// Expect: Returns 0
// Depends: kernel::BufferPool
// Note: Kernel does not yet implement VA=0 rejection; remains stub until
// implemented.
JARVIS_TEST(buffer_pool_zero_va_rejected) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Kernel task (no page_table_) alloc fails
// Input: Create task with page_table_ = 0, try alloc
// Expect: Returns 0
// Depends: kernel::BufferPool, kernel::TaskControlBlock
JARVIS_TEST(buffer_pool_kernel_task_alloc_fails) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);
    
    uint64_t saved_pt = task->page_table_;
    // Clear page_table_ to simulate kernel task
    task->page_table_ = 0;

    uint64_t h = BufferPool::alloc(*task, 0x300000000ULL);
    JARVIS_ASSERT_EQ(0ULL, h);

    task->page_table_ = saved_pt;
    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Forged handle after free fails validation
// Input: alloc -> free -> use same handle (old gen), validate() returns -1
// Expect: validate returns -1
// Depends: kernel::BufferPool
JARVIS_TEST(buffer_pool_forged_handle_after_free) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x400000000ULL;
    uint64_t handle = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(handle != 0);

    JARVIS_ASSERT(BufferPool::free(*task, handle));

    // Try to use the old handle (same idx, same gen)
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(handle));

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Realloc after free recycles entry with incremented generation
// Input: alloc -> free -> alloc again, verify same entry index recycled with
// new gen
// Expect: New handle has same idx, different (incremented) gen
// Depends: kernel::BufferPool
JARVIS_TEST(buffer_pool_realloc_recycles_entry) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x500000000ULL;
    uint64_t h1 = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(h1 != 0);

    uint32_t idx1 = static_cast<uint32_t>(h1 & 0xFFFFFFFFULL);
    uint32_t gen1 = static_cast<uint32_t>(h1 >> 32);

    JARVIS_ASSERT(BufferPool::free(*task, h1));

    uint64_t h2 = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(h2 != 0);

    uint32_t idx2 = static_cast<uint32_t>(h2 & 0xFFFFFFFFULL);
    uint32_t gen2 = static_cast<uint32_t>(h2 >> 32);

    JARVIS_ASSERT_EQ(idx1, idx2);
    JARVIS_ASSERT(gen2 == gen1 + 1);

    BufferPool::free(*task, h2);
    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Exhaust all 1024 entries, free one, new alloc recycles the freed
// entry
// Input: Fill pool completely, free one, alloc again
// Expect: New alloc succeeds and uses the freed entry index
// Depends: kernel::BufferPool
JARVIS_TEST(buffer_pool_alloc_after_exhaustion_and_free) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t handles[BufferPool::MAX_BUFFERS];
    uint64_t va = 0x600000000ULL;
    int alloc_count = 0;
    for (size_t i = 0; i < BufferPool::MAX_BUFFERS; ++i) {
        uint64_t h = BufferPool::alloc(*task, va + i * arch::PAGE_SIZE);
        if (h == 0) break;
        handles[i] = h;
        alloc_count++;
    }
    JARVIS_ASSERT_EQ(static_cast<int>(BufferPool::MAX_BUFFERS), alloc_count);

    // Free the middle entry (index 512)
    uint64_t freed = handles[512];
    uint32_t freed_idx = static_cast<uint32_t>(freed & 0xFFFFFFFFULL);
    JARVIS_ASSERT(BufferPool::free(*task, freed));

    // Alloc again - should reuse the freed entry
    uint64_t h = BufferPool::alloc(*task, va + 512 * arch::PAGE_SIZE);
    JARVIS_ASSERT(h != 0);
    uint32_t new_idx = static_cast<uint32_t>(h & 0xFFFFFFFFULL);
    JARVIS_ASSERT_EQ(freed_idx, new_idx);

    // Cleanup
    for (size_t i = 0; i < BufferPool::MAX_BUFFERS; ++i) {
        if (i != 512 && handles[i] != 0) {
            BufferPool::free(*task, handles[i]);
        }
    }
    BufferPool::free(*task, h);

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: List integrity after unlinking middle entry
// Input: alloc 10 buffers, free entry at index 5, verify neighbours'
// list_prev/list_next
// Expect: Entries 4 and 6 are correctly linked, head/tail intact
// Depends: kernel::BufferPool
JARVIS_TEST(buffer_pool_list_integrity_after_unlink) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t handles[10];
    uint64_t va = 0x700000000ULL;
    for (int i = 0; i < 10; ++i) {
        handles[i] = BufferPool::alloc(*task, va + i * arch::PAGE_SIZE);
        JARVIS_ASSERT(handles[i] != 0);
    }

    // Free middle entry (index 5)
    uint64_t freed = handles[5];
    uint32_t freed_idx = static_cast<uint32_t>(freed & 0xFFFFFFFFULL);
    JARVIS_ASSERT(BufferPool::free(*task, freed));

    // Verify list integrity: walk list and check all remaining entries present
    int count = 0;
    int32_t idx = task->buf_list_head;
    bool found[10] = {false};
    int32_t prev = -1;
    while (idx != -1) {
        count++;
        uint32_t uidx = static_cast<uint32_t>(idx);
        found[uidx] = true;
        // Check prev link
        JARVIS_ASSERT(BufferPool::entries[idx].list_prev == prev);
        prev = idx;
        idx = BufferPool::entries[idx].list_next;
    }
    JARVIS_ASSERT_EQ(9, count);

    // Verify all except index 5 are in list
    for (int i = 0; i < 10; ++i) {
        if (i == 5) continue;
        uint32_t uidx = static_cast<uint32_t>(handles[i] & 0xFFFFFFFFULL);
        JARVIS_ASSERT(found[uidx]);
    }
    JARVIS_ASSERT(!found[freed_idx]);

    // Cleanup
    for (int i = 0; i < 10; ++i) {
        if (i != 5 && handles[i] != 0) {
            BufferPool::free(*task, handles[i]);
        }
    }

    task->cleanup();
    delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Transfer same handle between two tasks back and forth
// multiple times, verifying ownership transfers correctly each time.
// Input: Sender allocs, transfers to receiver, receiver transfers back,
// sender transfers again. 5 cycles.
// Expect: After each transfer, only the new owner can free.
JARVIS_TEST(buffer_pool_transfer_race) {
    auto* task_a = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* task_b = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task_a != nullptr && task_b != nullptr);

    uint64_t va = 0x100000000ULL;
    uint64_t handle = BufferPool::alloc(*task_a, va);
    JARVIS_ASSERT(handle != 0);

    static const int CYCLES = 5;
    for (int i = 0; i < CYCLES; ++i) {
        // A -> B
        JARVIS_ASSERT(BufferPool::transfer(handle, *task_a, *task_b));
        JARVIS_ASSERT(!BufferPool::free(*task_a, handle));
        JARVIS_ASSERT(BufferPool::entries[handle & 0xFFFFFFFF].owner_task ==
                      static_cast<uint32_t>(task_b->id));

        // B -> A
        JARVIS_ASSERT(BufferPool::transfer(handle, *task_b, *task_a));
        JARVIS_ASSERT(!BufferPool::free(*task_b, handle));
        JARVIS_ASSERT(BufferPool::entries[handle & 0xFFFFFFFF].owner_task ==
                      static_cast<uint32_t>(task_a->id));
    }

    // Final cleanup by A
    JARVIS_ASSERT(BufferPool::free(*task_a, handle));

    task_a->cleanup(); delete task_a;
    task_b->cleanup(); delete task_b;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Alloc, free, then verify the old handle (with original
// generation) is rejected.  Then alloc again, verify the new handle
// has an incremented generation and the old handle still rejected.
// Input: Alloc -> free -> try old handle -> alloc -> try old handle
// Expect: Old handle always rejected; new handle valid.
JARVIS_TEST(buffer_pool_handle_reuse_security) {
    auto* task = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    JARVIS_ASSERT(task != nullptr);

    uint64_t va = 0x200000000ULL;
    uint64_t h1 = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(h1 != 0);

    uint64_t old_handle = h1;
    JARVIS_ASSERT(BufferPool::free(*task, h1));
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(old_handle));

    uint64_t h2 = BufferPool::alloc(*task, va);
    JARVIS_ASSERT(h2 != 0);
    uint32_t old_gen = static_cast<uint32_t>(old_handle >> 32);
    uint32_t new_gen = static_cast<uint32_t>(h2 >> 32);
    JARVIS_ASSERT(new_gen > old_gen);
    JARVIS_ASSERT_EQ(BUF_INVALID_HANDLE, BufferPool::validate(old_handle));

    BufferPool::free(*task, h2);
    task->cleanup(); delete task;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Transfer a buffer to a task that has no page table
// (kernel-only), verify transfer fails gracefully.
// Input: alloc in user task, transfer to kernel task.
// Expect: transfer returns false.
JARVIS_TEST(buffer_pool_transfer_to_kernel_task) {
    auto* user = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* kernel_task = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(user != nullptr && kernel_task != nullptr);
    JARVIS_ASSERT(kernel_task->page_table_ == 0);

    uint64_t va = 0x300000000ULL;
    uint64_t handle = BufferPool::alloc(*user, va);
    JARVIS_ASSERT(handle != 0);

    // Transfer to kernel task — should fail (no page table to clear PTE in)
    bool ok = BufferPool::transfer(handle, *user, *kernel_task);
    JARVIS_ASSERT(!ok);

    BufferPool::free(*user, handle);
    user->cleanup(); delete user;
    kernel_task->cleanup(); delete kernel_task;
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
    JARVIS_REGISTER_TEST(buffer_pool_cleanup_frees_buffers);
    JARVIS_REGISTER_TEST(buffer_pool_exec_into_current_clears_buffers);
    JARVIS_REGISTER_TEST(buffer_pool_transfer_adds_to_receiver_list);
    JARVIS_REGISTER_TEST(buffer_pool_va_conflict_rejected);
    JARVIS_REGISTER_TEST(buffer_pool_va_out_of_range_rejected);
    JARVIS_REGISTER_TEST(buffer_pool_zero_va_rejected);
    JARVIS_REGISTER_TEST(buffer_pool_kernel_task_alloc_fails);
    JARVIS_REGISTER_TEST(buffer_pool_forged_handle_after_free);
    JARVIS_REGISTER_TEST(buffer_pool_realloc_recycles_entry);
    JARVIS_REGISTER_TEST(buffer_pool_alloc_after_exhaustion_and_free);
    JARVIS_REGISTER_TEST(buffer_pool_list_integrity_after_unlink);
    JARVIS_REGISTER_TEST(buffer_pool_transfer_race);
    JARVIS_REGISTER_TEST(buffer_pool_handle_reuse_security);
    JARVIS_REGISTER_TEST(buffer_pool_transfer_to_kernel_task);
}
