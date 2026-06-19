#include <test.hpp>
#include <logger.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

TEST_CLASS(IpcMisformedMessages) {
    auto* cur = Scheduler::current_task();
    CT_ASSERT(cur != nullptr);
    CT_ASSERT(cur->msg_queue != nullptr);

    MessageQueue q;
    q.init();

    Message msg{};
    msg.sender_id = 1;
    msg.type = 99;
    msg.data_size = 4;
    msg.data[0] = 0xDE; msg.data[1] = 0xAD;

    msg.priority = 32;
    bool ok = q.push(msg);
    CT_ASSERT(ok);

    msg.priority = 0;
    msg.type = 1;
    ok = q.push(msg);
    CT_ASSERT(ok);

    Message out;
    ok = q.pop(out);
    CT_ASSERT(ok);
    CT_ASSERT(out.type == 1);

    ok = q.pop(out);
    CT_ASSERT(ok);
    CT_ASSERT(out.type == 99);

    CT_ASSERT(Scheduler::current_task() != nullptr);

    {
        MessageQueue q2;
        q2.init();
        msg.priority = 0;
        msg.data_size = IPC_MAX_MSG_SIZE + 1;
        msg.type = 42;
        ok = q2.push(msg);
        CT_ASSERT(ok);
        Message out2;
        ok = q2.pop(out2);
        CT_ASSERT(ok);
        CT_ASSERT(out2.data_size == IPC_MAX_MSG_SIZE + 1);
    }

    {
        MessageQueue q3;
        q3.init();
        msg.data_size = 0;
        msg.type = 43;
        ok = q3.push(msg);
        CT_ASSERT(ok);
        Message out3;
        ok = q3.pop(out3);
        CT_ASSERT(ok);
        CT_ASSERT(out3.data_size == 0);
    }
};

TEST_CLASS(IpcQueueWraparoundEdge) {
    MessageQueue q;
    q.init();
    Message msg{};
    msg.sender_id = 1;
    msg.priority = 0;
    msg.data_size = 0;

    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        msg.type = i;
        CT_ASSERT(q.push(msg));
    }
    CT_ASSERT(q.is_full());

    Message out;
    CT_ASSERT(q.pop(out));
    CT_ASSERT(out.type == 0ULL);

    msg.type = 99;
    CT_ASSERT(q.push(msg));
    CT_ASSERT(q.is_full());

    for (size_t i = 1; i < IPC_MAX_QUEUE_MSG; ++i) {
        CT_ASSERT(q.pop(out));
        CT_ASSERT(out.type == i);
    }
    CT_ASSERT(q.pop(out));
    CT_ASSERT(out.type == 99ULL);
    CT_ASSERT(q.is_empty());

    q.init();
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        msg.type = i;
        msg.priority = IPC_MAX_QUEUE_MSG - i;
        CT_ASSERT(q.push(msg));
    }
    CT_ASSERT(q.pop(out));
    CT_ASSERT(out.type == IPC_MAX_QUEUE_MSG - 1);
    CT_ASSERT(q.count == IPC_MAX_QUEUE_MSG - 1);

    size_t count = 0;
    while (q.pop(out)) ++count;
    CT_ASSERT(count == IPC_MAX_QUEUE_MSG - 1);
};

TEST_CLASS(IpcConcurrentSenders) {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);
    uint64_t recv_id = receiver->id;

    Message fill{};
    fill.sender_id = 0; fill.type = 0; fill.priority = 0; fill.data_size = 0;
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG / 2; ++i) {
        receiver->msg_queue->push(fill);
    }

    static const int NUM_SENDERS = 4;
    static const int MSGS_PER = 5;
    TaskControlBlock* senders[NUM_SENDERS];

    for (int i = 0; i < NUM_SENDERS; ++i) {
        senders[i] = TaskControlBlock::create([]() {}, 5, 10);
        CT_ASSERT(senders[i] != nullptr);
        Scheduler::add_task(*senders[i]);
    }

    auto* original = Scheduler::current_task();
    uint64_t total_sent = 0;

    for (int s = 0; s < NUM_SENDERS; ++s) {
        Scheduler::set_current(*senders[s]);
        for (int m = 0; m < MSGS_PER; ++m) {
            Message msg{};
            msg.sender_id = senders[s]->id;
            msg.type = static_cast<uint64_t>(s * MSGS_PER + m);
            msg.priority = 0;
            msg.data_size = 0;
            bool ok = IPC::send(recv_id, msg, IPC_NONBLOCK);
            if (ok) ++total_sent;
        }
    }

    Scheduler::set_current(*receiver);
    Message out;
    while (receiver->msg_queue->pop(out)) {}

    Scheduler::set_current(*original);

    CT_ASSERT(total_sent <= MSGS_PER * NUM_SENDERS);

    for (int i = 0; i < NUM_SENDERS; ++i) {
        Scheduler::remove_task(*senders[i]);
        senders[i]->cleanup();
        delete senders[i];
    }
    Scheduler::remove_task(*receiver);
    receiver->cleanup();
    delete receiver;
};

TEST_CLASS(IpcBufHandleTransferRoundtrip) {
    auto* sender = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    auto* receiver = TaskControlBlock::create_user([](){}, 5, 10, 32_KiB);
    CT_ASSERT(sender != nullptr);
    CT_ASSERT(receiver != nullptr);
    Scheduler::add_task(*sender);
    Scheduler::add_task(*receiver);

    auto* original = Scheduler::current_task();

    Scheduler::set_current(*sender);
    uint64_t sva = 0x80000000;
    uint64_t handle = BufferPool::alloc(*sender, sva);
    CT_ASSERT(handle != 0);

    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    uint64_t phys = BufferPool::entries[idx].phys_addr;
    CT_ASSERT(phys != 0);

    volatile auto* buf = reinterpret_cast<volatile uint8_t*>(
        arch::HHDM_OFFSET + phys);
    for (size_t i = 0; i < arch::PAGE_SIZE; ++i) {
        buf[i] = static_cast<uint8_t>(i ^ 0xA5);
    }

    Message msg{};
    msg.buf_handle = handle;
    msg.type = 100;
    msg.priority = 0;
    msg.data_size = 0;
    bool ok = IPC::send(receiver->id, msg, 0);
    CT_ASSERT(ok);

    Scheduler::set_current(*receiver);
    Message recv_msg{};
    ok = IPC::recv(recv_msg);
    CT_ASSERT(ok);
    CT_ASSERT(recv_msg.type == 100ULL);
    CT_ASSERT(recv_msg.buf_handle == handle);

    uint64_t rva = 0x90000000;
    ok = BufferPool::map(*receiver, handle, rva);
    CT_ASSERT(ok);

    CT_ASSERT(BufferPool::entries[idx].owner_task ==
              static_cast<uint32_t>(receiver->id));
    uint64_t rphys = BufferPool::entries[idx].phys_addr;
    CT_ASSERT(rphys == phys);
    volatile auto* rbuf = reinterpret_cast<volatile uint8_t*>(
        arch::HHDM_OFFSET + rphys);
    for (size_t i = 0; i < arch::PAGE_SIZE; ++i) {
        CT_ASSERT(rbuf[i] == static_cast<uint8_t>(i ^ 0xA5));
    }

    Scheduler::set_current(*sender);
    ok = BufferPool::free(*sender, handle);
    CT_ASSERT(!ok);

    Scheduler::set_current(*receiver);
    ok = BufferPool::free(*receiver, handle);
    CT_ASSERT(ok);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*sender);
    sender->cleanup(); delete sender;
    Scheduler::remove_task(*receiver);
    receiver->cleanup(); delete receiver;
};

TEST_CLASS(IpcBidirectionalSendSync) {
    static volatile uint64_t g_id_a = 0;
    static volatile uint64_t g_id_b = 0;

    auto* task_a = TaskControlBlock::create([]() {
        uint64_t b_id = g_id_b;
        Message req{}, reply{};
        req.sender_id = g_id_a;
        req.type = 10;
        req.priority = 0;
        req.data_size = 0;
        bool ok = IPC::send_sync(b_id, req, reply);
        JARVIS_ASSERT(ok);
        JARVIS_ASSERT(reply.type == 20ULL);
    }, 5, 10);
    CT_ASSERT(task_a != nullptr);
    g_id_a = task_a->id;
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create([]() {
        uint64_t a_id = g_id_a;
        Message msg{}, reply{};
        bool ok = IPC::recv(msg);
        JARVIS_ASSERT(ok);
        JARVIS_ASSERT(msg.type == 10ULL);
        reply.sender_id = g_id_b;
        reply.type = 20;
        reply.priority = 0;
        reply.data_size = 0;
        ok = IPC::send(msg.sender_id, reply);
        JARVIS_ASSERT(ok);

        Message req{};
        req.sender_id = g_id_b;
        req.type = 30;
        req.priority = 0;
        req.data_size = 0;
        ok = IPC::send_sync(a_id, req, reply);
        JARVIS_ASSERT(ok);
        JARVIS_ASSERT(reply.type == 40ULL);
    }, 5, 10);
    CT_ASSERT(task_b != nullptr);
    g_id_b = task_b->id;
    Scheduler::add_task(*task_b);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*task_a);
    Scheduler::reschedule();
    Scheduler::set_current(*task_b);
    Scheduler::reschedule();
    Scheduler::set_current(*task_a);
    Scheduler::reschedule();

    Scheduler::set_current(*original);

    Scheduler::remove_task(*task_a);
    task_a->cleanup(); delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup(); delete task_b;
};

TEST_CLASS(IpcBlockedSenderOnReceiverCleanup) {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto* sender = TaskControlBlock::create([]() {}, 6, 10);
    CT_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    Message fill{};
    fill.sender_id = 0; fill.type = 99; fill.priority = 0; fill.data_size = 0;
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        receiver->msg_queue->push(fill);
    }

    Message msg{};
    msg.sender_id = sender->id;
    msg.type = 1; msg.priority = 0; msg.data_size = 0;
    Scheduler::set_current(*sender);
    bool ok = IPC::send(receiver->id, msg, 0);
    CT_ASSERT(!ok);
    CT_ASSERT(sender->state == TaskState::BLOCKED);
    CT_ASSERT(receiver->msg_queue->blocked_senders_head == sender);

    receiver->state = TaskState::TERMINATED;
    receiver->exit_code = 0;
    receiver->cleanup();

    CT_ASSERT(sender->state == TaskState::READY);
    CT_ASSERT(sender->blocked_on_queue == nullptr);
    CT_ASSERT(receiver->msg_queue == nullptr);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*original);

    Scheduler::remove_task(*sender);
    Scheduler::remove_task(*receiver);
    sender->cleanup(); delete sender;
    delete receiver;
};

void register_ipc_robustness_tests() {
    Logger::info("Registering IPC robustness tests");
    REGISTER_CLASS(IpcMisformedMessages);
    REGISTER_CLASS(IpcQueueWraparoundEdge);
    REGISTER_CLASS(IpcConcurrentSenders);
    REGISTER_CLASS(IpcBufHandleTransferRoundtrip);
    REGISTER_CLASS(IpcBidirectionalSendSync);
    REGISTER_CLASS(IpcBlockedSenderOnReceiverCleanup);
}
