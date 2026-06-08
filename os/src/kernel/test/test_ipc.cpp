#include <test.hpp>
#include <logger.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

JARVIS_TEST(ipc_queue_init) {
    MessageQueue q;
    q.init();
    JARVIS_ASSERT(q.is_empty());
    JARVIS_ASSERT(!q.is_full());
    JARVIS_ASSERT_EQ(IPC_PRIORITY_LEVELS, q.highest_priority());
    JARVIS_ASSERT_EQ(0ULL, q.prio_bitmap);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_push_pop) {
    MessageQueue q;
    q.init();
    Message msg;
    msg.sender_id = 1;
    msg.type = 42;
    msg.priority = 0;
    msg.data_size = 4;
    msg.data[0] = 0xAA; msg.data[1] = 0xBB; msg.data[2] = 0xCC; msg.data[3] = 0xDD;

    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT(!q.is_empty());
    JARVIS_ASSERT_EQ(1ULL, q.count);

    Message out;
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
    MessageQueue q;
    q.init();
    Message msgs[4];
    for (int i = 0; i < 4; ++i) {
        msgs[i].sender_id = 100 + i;
        msgs[i].type = static_cast<uint64_t>(i);
        msgs[i].priority = 3 - static_cast<uint64_t>(i);
        msgs[i].data_size = 0;
    }
    for (int i = 0; i < 4; ++i) JARVIS_ASSERT(q.push(msgs[i]));

    JARVIS_ASSERT_EQ(0ULL, q.highest_priority());
    Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(3ULL, out.type);

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(2ULL, out.type);

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(1ULL, out.type);

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(0ULL, out.type);

    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_fifo_same_priority) {
    MessageQueue q;
    q.init();
    for (uint64_t i = 0; i < 5; ++i) {
        Message msg;
        msg.sender_id = i;
        msg.type = i * 10;
        msg.priority = 7;
        msg.data_size = 0;
        JARVIS_ASSERT(q.push(msg));
    }
    for (uint64_t i = 0; i < 5; ++i) {
        Message out;
        JARVIS_ASSERT(q.pop(out));
        JARVIS_ASSERT_EQ(i, out.sender_id);
        JARVIS_ASSERT_EQ(i * 10, out.type);
    }
    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_full) {
    MessageQueue q;
    q.init();
    Message msg;
    msg.sender_id = 1;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    JARVIS_ASSERT(q.is_full());
    JARVIS_ASSERT(!q.push(msg));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_empty_pop) {
    MessageQueue q;
    q.init();
    Message out;
    JARVIS_ASSERT(!q.pop(out));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_wrap_around) {
    MessageQueue q;
    q.init();
    Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG / 2; ++i) {
        Message out;
        JARVIS_ASSERT(q.pop(out));
    }
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG / 2; ++i) {
        JARVIS_ASSERT(q.push(msg));
    }
    size_t count = 0;
    Message out;
    while (q.pop(out)) ++count;
    JARVIS_ASSERT_EQ(IPC_MAX_QUEUE_MSG, count);
    JARVIS_ASSERT(q.is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_queue_highest_priority) {
    MessageQueue q;
    q.init();

    Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.data_size = 0;

    msg.priority = 15;
    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT_EQ(15ULL, q.highest_priority());

    msg.priority = 5;
    JARVIS_ASSERT(q.push(msg));
    JARVIS_ASSERT_EQ(5ULL, q.highest_priority());

    Message out;
    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(5ULL, out.priority);
    JARVIS_ASSERT_EQ(15ULL, q.highest_priority());

    JARVIS_ASSERT(q.pop(out));
    JARVIS_ASSERT_EQ(15ULL, out.priority);
    JARVIS_ASSERT_EQ(IPC_PRIORITY_LEVELS, q.highest_priority());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_send_recv_self) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    Message msg;
    msg.sender_id = cur->id;
    msg.type = 77;
    msg.priority = 0;
    msg.data_size = 0;

    bool ok = IPC::send(cur->id, msg);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(!cur->msg_queue->is_empty());
    JARVIS_ASSERT_EQ(1ULL, cur->msg_queue->count);

    Message out;
    ok = IPC::recv(out);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ(77ULL, out.type);
    JARVIS_ASSERT_EQ(cur->id, out.sender_id);
    JARVIS_ASSERT(cur->msg_queue->is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_send_nonexistent) {
    Message msg;
    msg.sender_id = 0;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    bool ok = IPC::send(999999, msg);
    JARVIS_ASSERT(!ok);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_send_nonblock_full) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    Message msg;
    msg.sender_id = cur->id;
    msg.type = 0;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        JARVIS_ASSERT(IPC::send(cur->id, msg));
    }
    JARVIS_ASSERT(cur->msg_queue->is_full());

    bool ok = IPC::send(cur->id, msg, IPC_NONBLOCK);
    JARVIS_ASSERT(!ok);

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        Message out;
        JARVIS_ASSERT(IPC::recv(out));
    }
    JARVIS_ASSERT(cur->msg_queue->is_empty());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_notify_basic) {
    sync::Notify n;
    n.init();
    JARVIS_ASSERT_EQ(0ULL, n.value());

    n.notify(42);
    JARVIS_ASSERT_EQ(42ULL, n.value());

    n.notify(99);
    JARVIS_ASSERT_EQ(99ULL, n.value());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_notify_try_wait) {
    sync::Notify n;
    n.init();

    uint64_t val = 0;
    bool ok = n.try_wait(&val);
    JARVIS_ASSERT(!ok);

    n.notify(55);
    ok = n.try_wait(&val);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ(55ULL, val);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(ipc_eventgroup_set_clear) {
    sync::EventGroup eg;
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
    sync::EventGroup eg;
    eg.init();

    JARVIS_ASSERT(!eg.try_wait_bits(0x01));

    eg.set_bits(0x03);
    JARVIS_ASSERT(eg.try_wait_bits(0x01));
    JARVIS_ASSERT(eg.try_wait_bits(0x02));
    JARVIS_ASSERT(eg.try_wait_bits(0x03));
    JARVIS_ASSERT(!eg.try_wait_bits(0x04));
    JARVIS_TEST_PASS();
}

void register_ipc_tests() {
    Logger::info("Registering IPC tests");

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
}
