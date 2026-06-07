#include <test.hpp>
#include <logger.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <error.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>
#include <version.hpp>

JARVIS_TEST(string_strlen) {
    JARVIS_ASSERT_EQ(0, strlen(""));
    JARVIS_ASSERT_EQ(5, strlen("hello"));
    JARVIS_ASSERT_EQ(13, strlen("Hello, World!"));
    JARVIS_ASSERT_EQ(1, strlen("x"));
    JARVIS_ASSERT_HEX_EQ(0, strlen("") ? 1 : 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strcmp) {
    JARVIS_ASSERT_EQ(0, strcmp("", ""));
    JARVIS_ASSERT_EQ(0, strcmp("abc", "abc"));
    JARVIS_ASSERT(strcmp("abc", "abd") < 0);
    JARVIS_ASSERT(strcmp("abd", "abc") > 0);
    JARVIS_ASSERT(strcmp("abc", "ab") != 0);
    JARVIS_ASSERT_EQ(0, strcmp("identical", "identical"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strncmp) {
    JARVIS_ASSERT_EQ(0, strncmp("abcde", "abcde", 5));
    JARVIS_ASSERT_EQ(0, strncmp("abcde", "abcxx", 3));
    JARVIS_ASSERT(strncmp("abcde", "abdde", 3) < 0);
    JARVIS_ASSERT_EQ(0, strncmp("", "", 0));
    JARVIS_ASSERT_EQ(0, strncmp("abcdef", "abc", 3));
    JARVIS_ASSERT(strncmp("abc", "abcdef", 6) < 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strncpy) {
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));
    strncpy(buf, "hello", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, "hello"));
    strncpy(buf, "", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, ""));
    strncpy(buf, "short", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, "short"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memcpy) {
    const char src[] = "memory test data";
    char dst[64];
    memset(dst, 0, sizeof(dst));
    memcpy(dst, src, sizeof(src));
    JARVIS_ASSERT_EQ(0, strcmp(dst, src));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memset) {
    char buf[32];
    memset(buf, 0xFF, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
        JARVIS_ASSERT(buf[i] == static_cast<char>(0xFF));
    memset(buf, 0, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
        JARVIS_ASSERT(buf[i] == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memcmp) {
    JARVIS_ASSERT_EQ(0, memcmp("abc", "abc", 3));
    JARVIS_ASSERT(memcmp("abc", "abd", 3) < 0);
    JARVIS_ASSERT(memcmp("abd", "abc", 3) > 0);
    JARVIS_ASSERT_EQ(0, memcmp("", "", 0));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_align_up) {
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0x1, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0x1000, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x2000, align_up(0x1001, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0xFFF, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x4, align_up(0x3, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x0, align_up(0x0, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(1, 0x1000));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_align_down) {
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0x1, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_down(0x1000, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_down(0x1001, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0xFFF, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0x0, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x0FFC, align_down(0x0FFF, 0x4));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_type_traits) {
    JARVIS_ASSERT((is_same<int, int>::value));
    JARVIS_ASSERT((!is_same<int, unsigned int>::value));
    JARVIS_ASSERT((is_same<uint64_t, unsigned long long>::value));
    JARVIS_ASSERT((is_integral<int>::value));
    JARVIS_ASSERT((is_integral<uint64_t>::value));
    JARVIS_ASSERT((!is_integral<double>::value));
    JARVIS_ASSERT((is_pod<int>::value));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(error_or_basic) {
    kernel::ErrorOr<int> e0(42);
    JARVIS_ASSERT(e0.ok());
    JARVIS_ASSERT_EQ(42, *e0);
    kernel::ErrorOr<int> e1(kernel::Error::OOM);
    JARVIS_ASSERT(!e1.ok());
    kernel::ErrorOr<void> v0;
    JARVIS_ASSERT(v0.ok());
    kernel::ErrorOr<void> v1(kernel::Error::INVALID_ARG);
    JARVIS_ASSERT(!v1.ok());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(error_or_errors) {
    kernel::ErrorOr<int> e_oom(kernel::Error::OOM);
    JARVIS_ASSERT(!e_oom.ok());
    JARVIS_ASSERT(e_oom.error == kernel::Error::OOM);
    kernel::ErrorOr<int> e_inv(kernel::Error::INVALID_ARG);
    JARVIS_ASSERT(e_inv.error == kernel::Error::INVALID_ARG);
    kernel::ErrorOr<int> e_nf(kernel::Error::NOT_FOUND);
    JARVIS_ASSERT(e_nf.error == kernel::Error::NOT_FOUND);
    JARVIS_ASSERT(e_nf.ok() == false);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_free) {
    uint64_t before = kernel::PMM::free_memory();
    uint64_t p1 = kernel::PMM::alloc_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(kernel::PMM::free_memory() == before - 4096);
    kernel::PMM::free_page(p1);
    JARVIS_ASSERT(kernel::PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_alloc_contiguous) {
    uint64_t before = kernel::PMM::free_memory();
    uint64_t pages = kernel::PMM::alloc_contiguous(4);
    JARVIS_ASSERT(pages != 0);
    JARVIS_ASSERT(kernel::PMM::free_memory() <= before - 4 * 4096);
    for (size_t i = 0; i < 4; ++i)
        kernel::PMM::free_page(pages + i * 4096);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_user_alloc) {
    uint64_t before = kernel::PMM::free_memory();
    uint64_t p1 = kernel::PMM::alloc_user_page();
    JARVIS_ASSERT(p1 != 0);
    JARVIS_ASSERT(kernel::PMM::is_user_page(p1));
    kernel::PMM::free_page(p1);
    JARVIS_ASSERT(kernel::PMM::free_memory() == before);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(pmm_total_memory) {
    uint64_t total = kernel::PMM::total_memory();
    JARVIS_ASSERT(total > 0);
    JARVIS_ASSERT(total >= kernel::PMM::free_memory());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_alloc_free) {
    void* p1 = kernel::MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    void* p2 = kernel::MemPool::alloc(32);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p2 != p1);
    kernel::MemPool::free(p1);
    kernel::MemPool::free(p2);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_large_alloc) {
    void* p = kernel::MemPool::alloc(1024);
    JARVIS_ASSERT(p != nullptr);
    kernel::MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_fragmentation) {
    void* ptrs[10];
    for (int i = 0; i < 10; ++i) {
        ptrs[i] = kernel::MemPool::alloc(64);
        JARVIS_ASSERT(ptrs[i] != nullptr);
    }
    for (int i = 0; i < 10; ++i)
        kernel::MemPool::free(ptrs[i]);
    void* p = kernel::MemPool::alloc(64);
    JARVIS_ASSERT(p != nullptr);
    kernel::MemPool::free(p);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(mempool_reuse) {
    void* p1 = kernel::MemPool::alloc(16);
    JARVIS_ASSERT(p1 != nullptr);
    kernel::MemPool::free(p1);
    void* p2 = kernel::MemPool::alloc(16);
    JARVIS_ASSERT(p2 != nullptr);
    JARVIS_ASSERT(p1 == p2);
    kernel::MemPool::free(p2);
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// Priority MessageQueue tests (standalone, no TCB dependency)
// ---------------------------------------------------------------------------

JARVIS_TEST(ipc_queue_init) {
    kernel::MessageQueue q;
    q.init();
    JARVIS_ASSERT(q.is_empty());
    JARVIS_ASSERT(!q.is_full());
    JARVIS_ASSERT_EQ(kernel::IPC_PRIORITY_LEVELS, q.highest_priority());
    JARVIS_ASSERT_EQ(0ULL, q.prio_bitmap);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_push_pop) {
    kernel::MessageQueue q;
    q.init();
    kernel::Message msg;
    msg.sender_id = 1;
    msg.type = 42;
    msg.priority = 0;
    msg.data_size = 4;
    msg.data[0] = 0xAA; msg.data[1] = 0xBB; msg.data[2] = 0xCC; msg.data[3] = 0xDD;

    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT(!q.is_empty());
    JARVIS_ASSERT_EQ(1ULL, q.count);

    kernel::Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(1ULL, out.sender_id);
    JARVIS_ASSERT_EQ(42ULL, out.type);
    JARVIS_ASSERT_EQ(0ULL, out.priority);
    JARVIS_ASSERT_EQ(4ULL, out.data_size);
    JARVIS_ASSERT_EQ(0xAA, out.data[0]);
    JARVIS_ASSERT_EQ(0xBB, out.data[1]);
    JARVIS_ASSERT_EQ(0xCC, out.data[2]);
    JARVIS_ASSERT_EQ(0xDD, out.data[3]);
    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_priority_order) {
    kernel::MessageQueue q;
    q.init();
    kernel::Message msgs[4];
    for (int i = 0; i < 4; ++i) {
        msgs[i].sender_id = 100 + i;
        msgs[i].type = static_cast<uint64_t>(i);
        msgs[i].priority = 3 - static_cast<uint64_t>(i);  // priorities: 3, 2, 1, 0
        msgs[i].data_size = 0;
    }
    // push in order 0,1,2,3 (priorities 3,2,1,0 — descending)
    for (int i = 0; i < 4; ++i) JARVIS_ASSERT(q.push(msgs[i]));

    // pop should return in priority order: 0 (prio 0), then 1 (prio 1), 2 (prio 2), 3 (prio 3)
    // actually: lower priority number = higher urgency, so priority 0 should pop first
    JARVIS_ASSERT_EQ(0ULL, q.highest_priority());
    kernel::Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(3ULL, out.type);  // sent as i=3, priority 0

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(2ULL, out.type);  // sent as i=2, priority 1

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(1ULL, out.type);  // sent as i=1, priority 2

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(0ULL, out.type);  // sent as i=0, priority 3

    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_fifo_same_priority) {
    kernel::MessageQueue q;
    q.init();
    for (uint64_t i = 0; i < 5; ++i) {
        kernel::Message msg;
        msg.sender_id = i;
        msg.type = i * 10;
        msg.priority = 7;  // all same priority
        msg.data_size = 0;
        JARVIS_ASSERT(q.push(msg));
    }
    for (uint64_t i = 0; i < 5; ++i) {
        kernel::Message out;
        JARVIS_ASSERT(q.pop(out));
        JARVIS_ASSERT_EQ(i, out.sender_id);
        JARVIS_ASSERT_EQ(i * 10, out.type);
    }
    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_full) {
    kernel::MessageQueue q;
    q.init();
    kernel::Message msg;
    msg.sender_id = 1;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    JARVIS_ASSERT(q.is_full());
    JARVIS_ASSERT(!q.push(msg));  // should fail
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_empty_pop) {
    kernel::MessageQueue q;
    q.init();
    kernel::Message out;
    JARVIS_ASSERT(!q.pop(out));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_wrap_around) {
    kernel::MessageQueue q;
    q.init();
    kernel::Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    // fill queue
    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    // drain half
    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG / 2; ++i) {
        kernel::Message out;
        JARVIS_ASSERT(q.pop(out));
    }
    // fill again — this exercises wrap-around in the circular buffer
    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG / 2; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    // drain all
    size_t count = 0;
    kernel::Message out;
    while (q.pop(out)) ++count;
    JARVIS_ASSERT_EQ(kernel::IPC_MAX_QUEUE_MSG, count);
    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_highest_priority) {
    kernel::MessageQueue q;
    q.init();

    kernel::Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.data_size = 0;

    // Push at priority 15
    msg.priority = 15;
    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT_EQ(15ULL, q.highest_priority());

    // Push at priority 5 (higher urgency)
    msg.priority = 5;
    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT_EQ(5ULL, q.highest_priority());

    // Pop the priority 5 message
    kernel::Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(5ULL, out.priority);
    JARVIS_ASSERT_EQ(15ULL, q.highest_priority());

    // Pop the priority 15 message
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(15ULL, out.priority);
    JARVIS_ASSERT_EQ(kernel::IPC_PRIORITY_LEVELS, q.highest_priority());
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// IPC send/recv tests (needs scheduler + current task)
// ---------------------------------------------------------------------------

JARVIS_TEST(ipc_send_recv_self) {
    auto* cur = kernel::Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    kernel::Message msg;
    msg.sender_id = cur->id;
    msg.type = 77;
    msg.priority = 0;
    msg.data_size = 0;

    bool ok = kernel::IPC::send(cur->id, msg);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(!cur->msg_queue->is_empty());
    JARVIS_ASSERT_EQ(1ULL, cur->msg_queue->count);

    kernel::Message out;
    ok = kernel::IPC::recv(out);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ(77ULL, out.type);
    JARVIS_ASSERT_EQ(cur->id, out.sender_id);
    JARVIS_ASSERT(cur->msg_queue->is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_send_nonexistent) {
    // send to a task ID that does not exist
    kernel::Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    bool ok = kernel::IPC::send(999999, msg);
    JARVIS_ASSERT(!ok);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_send_nonblock_full) {
    auto* cur = kernel::Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    kernel::Message msg;
    msg.sender_id = cur->id;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    // fill the queue
    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(kernel::IPC::send(cur->id, msg));
    }
    JARVIS_ASSERT(cur->msg_queue->is_full());

    // send with NONBLOCK should fail
    bool ok = kernel::IPC::send(cur->id, msg, kernel::IPC_NONBLOCK);
    JARVIS_ASSERT(!ok);

    // drain and verify
    for (size_t i = 0; i < kernel::IPC_MAX_QUEUE_MSG; ++i) {
        kernel::Message out;
        JARVIS_ASSERT(kernel::IPC::recv(out));
    }
    JARVIS_ASSERT(cur->msg_queue->is_empty());
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// sync::Notify tests
// ---------------------------------------------------------------------------

JARVIS_TEST(ipc_notify_basic) {
    kernel::sync::Notify n;
    n.init();
    JARVIS_ASSERT_EQ(0ULL, n.value());

    n.notify(42);
    JARVIS_ASSERT_EQ(42ULL, n.value());

    n.notify(99);
    JARVIS_ASSERT_EQ(99ULL, n.value());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_notify_try_wait) {
    kernel::sync::Notify n;
    n.init();

    uint64_t val = 0;
    // no notification yet → try_wait should succeed (returns true, val may be 0)
    // Actually try_wait returns true if no waiter is pending.
    // With Notify::try_wait: if (waiter_ == nullptr && value) { *value = notify_value_; return true; }
    // So if notify_value_ is 0 and no waiter, the condition fails!
    // Let me just check behavior:
    bool ok = n.try_wait(&val);
    // When notify_value_ is 0, try_wait returns false because `value` check fails
    JARVIS_ASSERT(!ok);

    n.notify(55);
    ok = n.try_wait(&val);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ(55ULL, val);
    JARVIS_TEST_PASS();
}

// ---------------------------------------------------------------------------
// sync::EventGroup tests
// ---------------------------------------------------------------------------

JARVIS_TEST(ipc_eventgroup_set_clear) {
    kernel::sync::EventGroup eg;
    eg.init();
    JARVIS_ASSERT_EQ(0ULL, eg.get_bits());

    eg.set_bits(0x0F);
    JARVIS_ASSERT_EQ(0x0FULL, eg.get_bits());

    eg.clear_bits(0x05);
    JARVIS_ASSERT_EQ(0x0AULL, eg.get_bits());

    eg.set_bits(0xF0);
    JARVIS_ASSERT_EQ(0xFAULL, eg.get_bits());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_eventgroup_try_wait) {
    kernel::sync::EventGroup eg;
    eg.init();

    // no bits set → try_wait for any bits should fail
    JARVIS_ASSERT(!eg.try_wait_bits(0x01));

    eg.set_bits(0x03);
    JARVIS_ASSERT(eg.try_wait_bits(0x01));
    JARVIS_ASSERT(eg.try_wait_bits(0x02));
    JARVIS_ASSERT(eg.try_wait_bits(0x03));
    JARVIS_ASSERT(!eg.try_wait_bits(0x04));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(driver_registry_find) {
    auto* drv = kernel::DriverRegistry::find("keyboard");
    JARVIS_ASSERT(drv != nullptr);
    JARVIS_ASSERT_EQ(0, strcmp(drv->name, "keyboard"));
    drv = kernel::DriverRegistry::find("nonexistent_driver_xyz");
    JARVIS_ASSERT(drv == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(driver_registry_count) {
    size_t cnt = kernel::DriverRegistry::count();
    JARVIS_ASSERT(cnt >= 4);
    bool has_kbd = false, has_timer = false;
    for (size_t i = 0; i < cnt; ++i) {
        auto* d = kernel::DriverRegistry::get(i);
        if (d) {
            if (strcmp(d->name, "keyboard") == 0) has_kbd = true;
            if (strcmp(d->name, "timer") == 0) has_timer = true;
        }
    }
    JARVIS_ASSERT(has_kbd);
    JARVIS_ASSERT(has_timer);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_task_count) {
    uint64_t cnt = kernel::Scheduler::task_count();
    JARVIS_ASSERT(cnt >= 1);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(scheduler_current_task) {
    auto* cur = kernel::Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->id > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_fdtable_alloc_free) {
    kernel::vfs::FdTable ft;
    int fd1 = ft.alloc();
    JARVIS_ASSERT(fd1 >= 0);
    JARVIS_ASSERT(ft.get(fd1) != nullptr);
    JARVIS_ASSERT(ft.get(fd1)->used);
    ft.free(fd1);
    JARVIS_ASSERT(ft.get(fd1) == nullptr || !ft.get(fd1)->used);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_fdtable_multiple) {
    kernel::vfs::FdTable ft;
    int fds[5];
    for (int i = 0; i < 5; ++i) {
        fds[i] = ft.alloc();
        JARVIS_ASSERT(fds[i] >= 0);
    }
    for (int i = 0; i < 5; ++i)
        ft.free(fds[i]);
    int fd = ft.alloc();
    JARVIS_ASSERT(fd >= 0);
    ft.free(fd);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_root) {
    auto* vn = kernel::vfs::resolve("/");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & kernel::vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_dev) {
    auto* vn = kernel::vfs::resolve("/dev");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & kernel::vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_tty) {
    auto* vn = kernel::vfs::resolve("/dev/tty");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & kernel::vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_null) {
    auto* vn = kernel::vfs::resolve("/dev/null");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & kernel::vfs::S_IFCHR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_proc) {
    auto* vn = kernel::vfs::resolve("/proc");
    JARVIS_ASSERT(vn != nullptr);
    JARVIS_ASSERT(vn->mode & kernel::vfs::S_IFDIR);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(vfs_resolve_nonexistent) {
    auto* vn = kernel::vfs::resolve("/nonexistent_path_xyz");
    JARVIS_ASSERT(vn == nullptr);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_string_not_empty) {
    const char* sv = kernel::Version::string();
    JARVIS_ASSERT(sv != nullptr);
    JARVIS_ASSERT(strlen(sv) > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_full_string_not_empty) {
    const char* fv = kernel::Version::full_string();
    JARVIS_ASSERT(fv != nullptr);
    JARVIS_ASSERT(strlen(fv) > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_build_date_not_empty) {
    const char* bd = kernel::Version::build_date();
    JARVIS_ASSERT(bd != nullptr);
    JARVIS_ASSERT(strlen(bd) > 0);
    JARVIS_TEST_PASS();
}

void register_selftest_tests() {
    kernel::Logger::info("Registering selftest suite");

    JARVIS_REGISTER_TEST(string_strlen);
    JARVIS_REGISTER_TEST(string_strcmp);
    JARVIS_REGISTER_TEST(string_strncmp);
    JARVIS_REGISTER_TEST(string_strncpy);
    JARVIS_REGISTER_TEST(string_memcpy);
    JARVIS_REGISTER_TEST(string_memset);
    JARVIS_REGISTER_TEST(string_memcmp);

    JARVIS_REGISTER_TEST(utils_align_up);
    JARVIS_REGISTER_TEST(utils_align_down);
    JARVIS_REGISTER_TEST(utils_type_traits);

    JARVIS_REGISTER_TEST(error_or_basic);
    JARVIS_REGISTER_TEST(error_or_errors);

    JARVIS_REGISTER_TEST(pmm_alloc_free);
    JARVIS_REGISTER_TEST(pmm_alloc_contiguous);
    JARVIS_REGISTER_TEST(pmm_user_alloc);
    JARVIS_REGISTER_TEST(pmm_total_memory);

    JARVIS_REGISTER_TEST(mempool_alloc_free);
    JARVIS_REGISTER_TEST(mempool_large_alloc);
    JARVIS_REGISTER_TEST(mempool_fragmentation);
    JARVIS_REGISTER_TEST(mempool_reuse);

    JARVIS_REGISTER_TEST(ipc_queue_init);
    JARVIS_REGISTER_TEST(ipc_queue_push_pop);
    JARVIS_REGISTER_TEST(ipc_queue_priority_order);
    JARVIS_REGISTER_TEST(ipc_queue_fifo_same_priority);
    JARVIS_REGISTER_TEST(ipc_queue_full);
    JARVIS_REGISTER_TEST(ipc_queue_empty_pop);
    JARVIS_REGISTER_TEST(ipc_queue_wrap_around);
    JARVIS_REGISTER_TEST(ipc_queue_highest_priority);
    JARVIS_REGISTER_TEST(ipc_send_recv_self);
    JARVIS_REGISTER_TEST(ipc_send_nonexistent);
    JARVIS_REGISTER_TEST(ipc_send_nonblock_full);
    JARVIS_REGISTER_TEST(ipc_notify_basic);
    JARVIS_REGISTER_TEST(ipc_notify_try_wait);
    JARVIS_REGISTER_TEST(ipc_eventgroup_set_clear);
    JARVIS_REGISTER_TEST(ipc_eventgroup_try_wait);

    JARVIS_REGISTER_TEST(driver_registry_find);
    JARVIS_REGISTER_TEST(driver_registry_count);

    JARVIS_REGISTER_TEST(scheduler_task_count);
    JARVIS_REGISTER_TEST(scheduler_current_task);

    JARVIS_REGISTER_TEST(vfs_fdtable_alloc_free);
    JARVIS_REGISTER_TEST(vfs_fdtable_multiple);
    JARVIS_REGISTER_TEST(vfs_resolve_root);
    JARVIS_REGISTER_TEST(vfs_resolve_dev);
    JARVIS_REGISTER_TEST(vfs_resolve_tty);
    JARVIS_REGISTER_TEST(vfs_resolve_null);
    JARVIS_REGISTER_TEST(vfs_resolve_proc);
    JARVIS_REGISTER_TEST(vfs_resolve_nonexistent);

    JARVIS_REGISTER_TEST(version_string_not_empty);
    JARVIS_REGISTER_TEST(version_full_string_not_empty);
    JARVIS_REGISTER_TEST(version_build_date_not_empty);
}
