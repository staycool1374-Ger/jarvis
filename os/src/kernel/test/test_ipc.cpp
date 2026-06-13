#include <test.hpp>
#include <logger.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies that a freshly initialized MessageQueue reports empty,
// not full, with correct priority level and zero bitmap.
// Input: None (basic init)
// Expect: is_empty() == true, is_full() == false, highest_priority() ==
// IPC_PRIORITY_LEVELS, prio_bitmap == 0
// Depends: kernel::MessageQueue
JARVIS_TEST(ipc_queue_init) {
    MessageQueue q;
    q.init();
    JARVIS_ASSERT(q.is_empty());
    JARVIS_ASSERT(!q.is_full());
    JARVIS_ASSERT_EQ(IPC_PRIORITY_LEVELS, q.highest_priority());
    JARVIS_ASSERT_EQ(0ULL, q.prio_bitmap);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that a message can be pushed and popped correctly,
// preserving all fields.
// Input: msg.sender_id=1, msg.type=42, msg.priority=0, msg.data_size=4,
// msg.data = 0xAA,0xBB,0xCC,0xDD
// Expect: push returns true, pop returns matching
// sender_id/type/priority/data_size/data, queue empty after pop
// Depends: kernel::MessageQueue
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

// Runmode: kernel
// Testidea: Verifies that messages are dequeued in priority order (lowest
// priority value = highest priority).
// Input: Four messages with priorities 3, 2, 1, 0 pushed in that order
// Expect: Popped types are 3, 2, 1, 0 (priority 0 dequeued first, priority 3
// last)
// Depends: kernel::MessageQueue
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

// Runmode: kernel
// Testidea: Verifies FIFO ordering for messages with the same priority level.
// Input: Five messages with priority=7, sender_id=0..4, type=i*10, pushed
// sequentially
// Expect: Popped in same order (sender_id 0..4, type 0,10,20,30,40)
// Depends: kernel::MessageQueue
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

// Runmode: kernel
// Testidea: Verifies that the queue correctly reports full after
// IPC_MAX_QUEUE_MSG pushes and rejects subsequent pushes.
// Input: IPC_MAX_QUEUE_MSG pushes of a trivial message, then one extra push
// Expect: First IPC_MAX_QUEUE_MSG pushes succeed, is_full() true, extra push
// returns false
// Depends: kernel::MessageQueue, IPC_MAX_QUEUE_MSG
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

// Runmode: kernel
// Testidea: Verifies that popping from an empty MessageQueue returns false.
// Input: None (newly initialized queue)
// Expect: q.pop(out) returns false
// Depends: kernel::MessageQueue
JARVIS_TEST(ipc_queue_empty_pop) {
    MessageQueue q;
    q.init();
    Message out;
    JARVIS_ASSERT(!q.pop(out));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that the ring-buffer MessageQueue handles wrap-around
// correctly by filling, partially draining, then refilling.
// Input: Fill queue to IPC_MAX_QUEUE_MSG, pop half, push half again, then
// drain all
// Expect: Total drained count == IPC_MAX_QUEUE_MSG, queue empty at end
// Depends: kernel::MessageQueue, IPC_MAX_QUEUE_MSG
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

// Runmode: kernel
// Testidea: Verifies that highest_priority() correctly tracks the minimum
// priority value in the queue as messages are pushed and popped.
// Input: Push priority 15, then priority 5; pop priority 5, then priority 15
// Expect: After push(15) -> highest=15, after push(5) -> highest=5, after
// pop(5) -> highest=15, after pop(15) -> highest=IPC_PRIORITY_LEVELS
// Depends: kernel::MessageQueue, IPC_PRIORITY_LEVELS
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

// Runmode: kernel
// Testidea: Verifies that IPC::send and IPC::recv work for self-messaging
// (sender == receiver).
// Input: msg type=77, priority=0, sent to own task ID
// Expect: send returns true, recv returns true, received type=77, sender_id
// matches current task, queue empty after recv
// Depends: kernel::MessageQueue, kernel::IPC, kernel::Scheduler
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

// Runmode: kernel
// Testidea: Verifies that sending to a nonexistent task ID returns false.
// Input: IPC::send(999999, msg) where 999999 does not match any task
// Expect: send returns false
// Depends: kernel::IPC, kernel::Scheduler
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

// Runmode: kernel
// Testidea: Verifies that IPC::send with IPC_NONBLOCK flag returns false
// when the target queue is full.
// Input: Fill own queue to IPC_MAX_QUEUE_MSG, then send with IPC_NONBLOCK
// Expect: Non-blocking send returns false while queue is full; after
// draining, all recvs succeed and queue is empty
// Depends: kernel::MessageQueue, kernel::IPC, kernel::Scheduler,
// IPC_MAX_QUEUE_MSG, IPC_NONBLOCK
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

// Runmode: kernel
// Testidea: Verifies that Notify can store and overwrite a notification value.
// Input: n.notify(42), then n.notify(99)
// Expect: n.value() == 42 after first notify, n.value() == 99 after second
// Depends: kernel::sync::Notify
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

// Runmode: kernel
// Testidea: Verifies that Notify::try_wait consumes a pending notification
// value.
// Input: try_wait on empty notify, then notify(55) and try_wait again
// Expect: First try_wait returns false; second returns true with val==55
// Depends: kernel::sync::Notify
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

// Runmode: kernel
// Testidea: Verifies that EventGroup::set_bits and clear_bits manipulate the
// bitmask correctly.
// Input: set_bits(0x0F), clear_bits(0x05), set_bits(0xF0)
// Expect: get_bits() == 0x00 -> 0x0F -> 0x0A -> 0xFA
// Depends: kernel::sync::EventGroup
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

// Runmode: kernel
// Testidea: Verifies that EventGroup::try_wait_bits checks whether specific
// bits are set without consuming them.
// Input: set_bits(0x03), then try_wait for 0x01, 0x02, 0x03, 0x04
// Expect: try_wait(0x01) true, try_wait(0x02) true, try_wait(0x03) true,
// try_wait(0x04) false
// Depends: kernel::sync::EventGroup
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

// Runmode: kernel
// Testidea: Verifies that IPC::block_sender adds a sender TCB to the
// MessageQueue's blocked-senders linked list.
// Input: Two sender tasks created and blocked via IPC::block_sender
// Expect: blocked_senders_head/tail point to correct senders, blocked_next
// links are set
// Depends: kernel::MessageQueue, kernel::IPC, kernel::TaskControlBlock,
// kernel::Scheduler
JARVIS_TEST(ipc_block_sender_adds_to_list) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    MessageQueue& q = *cur->msg_queue;
    JARVIS_ASSERT(q.blocked_senders_head == nullptr);
    JARVIS_ASSERT(q.blocked_senders_tail == nullptr);

    IPC::block_sender(q, *sender);

    JARVIS_ASSERT(q.blocked_senders_head == sender);
    JARVIS_ASSERT(q.blocked_senders_tail == sender);
    JARVIS_ASSERT(sender->blocked_next == nullptr);

    auto* sender2 = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender2 != nullptr);
    Scheduler::add_task(*sender2);
    IPC::block_sender(q, *sender2);

    JARVIS_ASSERT(q.blocked_senders_head == sender);
    JARVIS_ASSERT(q.blocked_senders_tail == sender2);
    JARVIS_ASSERT(sender->blocked_next == sender2);
    JARVIS_ASSERT(sender2->blocked_next == nullptr);

    IPC::wake_sender(q, *cur);
    IPC::wake_sender(q, *cur);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    Scheduler::remove_task(*sender2);
    sender2->cleanup();
    delete sender2;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that IPC::wake_sender removes a blocked sender from the
// list and sets its state to READY.
// Input: Block one sender, then wake it
// Expect: blocked_senders_head/tail become nullptr, sender state is
// TaskState::READY
// Depends: kernel::MessageQueue, kernel::IPC, kernel::TaskControlBlock,
// kernel::Scheduler
JARVIS_TEST(ipc_wake_sender_removes_from_list) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    MessageQueue& q = *cur->msg_queue;
    q.blocked_senders_head = nullptr;
    q.blocked_senders_tail = nullptr;
    IPC::block_sender(q, *sender);

    JARVIS_ASSERT(q.blocked_senders_head == sender);

    IPC::wake_sender(q, *cur);

    JARVIS_ASSERT(q.blocked_senders_head == nullptr);
    JARVIS_ASSERT(q.blocked_senders_tail == nullptr);
    JARVIS_ASSERT(sender->state == TaskState::READY);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that IPC::wake_sender handles a terminated sender
// gracefully by removing it from the list.
// Input: Block a sender, mark it TERMINATED, then wake it
// Expect: blocked_senders_head/tail become nullptr (sender removed from list
// despite terminated state)
// Depends: kernel::MessageQueue, kernel::IPC, kernel::TaskControlBlock,
// kernel::Scheduler
JARVIS_TEST(ipc_wake_sender_terminated) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    MessageQueue& q = *cur->msg_queue;
    q.blocked_senders_head = nullptr;
    q.blocked_senders_tail = nullptr;
    IPC::block_sender(q, *sender);

    sender->state = TaskState::TERMINATED;

    IPC::wake_sender(q, *cur);

    JARVIS_ASSERT(q.blocked_senders_head == nullptr);
    JARVIS_ASSERT(q.blocked_senders_tail == nullptr);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that IPC::wake_sender restores the receiver's priority
// to its base_priority.
// Input: Set cur->base_priority=5, cur->priority=10, block/wake a sender
// Expect: After wake_sender, cur->priority == 5 (base_priority)
// Depends: kernel::MessageQueue, kernel::IPC, kernel::TaskControlBlock,
// kernel::Scheduler
JARVIS_TEST(ipc_wake_sender_restores_priority) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);
    JARVIS_ASSERT(cur->msg_queue != nullptr);

    cur->base_priority = 5;
    cur->priority = 10;

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    MessageQueue& q = *cur->msg_queue;
    IPC::block_sender(q, *sender);

    IPC::wake_sender(q, *cur);

    JARVIS_ASSERT_EQ(5ULL, cur->priority);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that IPC::send blocks the sender when the target queue
// is full, and unblocks the sender when the receiver pops a message.
// Input: Fill own queue, create sender task, switch to sender and attempt
// send (blocks), switch back to receiver and recv
// Expect: Sender is BLOCKED after failed send, becomes READY after receiver
// pops, send returns false
// Depends: kernel::MessageQueue, kernel::IPC, kernel::TaskControlBlock,
// kernel::Scheduler, IPC_MAX_QUEUE_MSG
JARVIS_TEST(ipc_send_block_full) {
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

    auto* sender = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    Scheduler::set_current(*sender);
    bool ok = IPC::send(cur->id, msg);
    JARVIS_ASSERT(!ok);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    Scheduler::set_current(*cur);
    Message out;
    JARVIS_ASSERT(IPC::recv(out));
    JARVIS_ASSERT(sender->state == TaskState::READY);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies synchronous IPC send/receive round-trip between two tasks.
// Input: Create sender and receiver tasks. Sender calls send_sync with a
// message, receiver recvs and replies.
// Expect: send_sync returns true, received message matches, reply received
// correctly.
JARVIS_TEST(ipc_send_sync_roundtrip) {
    static uint64_t g_receiver_id = 0;

    auto* receiver = TaskControlBlock::create([]() {
        Message msg;
        // Receiver waits for message
        JARVIS_ASSERT(IPC::recv(msg));
        JARVIS_ASSERT_EQ(42ULL, msg.type);
        // Send reply
        Message reply;
        reply.sender_id = Scheduler::current_task()->id;
        reply.type = 99;
        reply.priority = 0;
        reply.data_size = 0;
        JARVIS_ASSERT(IPC::send(msg.sender_id, reply));
    }, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    g_receiver_id = receiver->id;
    Scheduler::add_task(*receiver);

    auto* sender = TaskControlBlock::create([]() {
        Message msg;
        msg.sender_id = Scheduler::current_task()->id;
        msg.type = 42;
        msg.priority = 0;
        msg.data_size = 0;

        Message reply;
        bool ok = IPC::send_sync(g_receiver_id, msg, reply);
        JARVIS_ASSERT(ok);
        JARVIS_ASSERT_EQ(99ULL, reply.type);
    }, 5, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*sender);

    // Let sender run (it will block on send_sync)
    // Then receiver runs, recvs, replies
    Scheduler::reschedule();
    Scheduler::reschedule();

    Scheduler::set_current(*original);

    Scheduler::remove_task(*sender);
    sender->cleanup();
    delete sender;
    Scheduler::remove_task(*receiver);
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that blocked IPC senders are unblocked when the
// receiver task exits.
// Input: Create receiver and sender. Fill receiver's queue, sender blocks on
// send. Terminate receiver and cleanup.
// Expect: Sender is woken up (state becomes READY) and removed from blocked
// list.
JARVIS_TEST(ipc_sender_unblocked_on_receiver_exit) {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    Scheduler::add_task(*receiver);

    auto* sender = TaskControlBlock::create([]() {}, 6, 10);
    JARVIS_ASSERT(sender != nullptr);
    Scheduler::add_task(*sender);

    kernel::Message msg{};
    msg.sender_id = sender->id;
    msg.type = 1;
    msg.priority = 0;
    msg.data_size = 0;

    // Fill receiver's queue to force sender to block
    for (size_t i = 0; i < IPC_MAX_QUEUE_MSG; ++i) {
        kernel::Message fill_msg{};
        fill_msg.sender_id = 0;
        fill_msg.type = 99;
        fill_msg.priority = 0;
        fill_msg.data_size = 0;
        receiver->msg_queue->push(fill_msg);
    }

    // Now send from sender - should block
    Scheduler::set_current(*sender);
    (void)kernel::IPC::send(receiver->id, msg, 0);
    // The sender should be in blocked list
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == sender);
    JARVIS_ASSERT(sender->state == TaskState::BLOCKED);

    // Now terminate receiver and cleanup
    receiver->state = TaskState::TERMINATED;
    receiver->exit_code = 0;
    receiver->cleanup();

    // Sender should be woken up
    JARVIS_ASSERT(sender->state == TaskState::READY);
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_head == nullptr);
    JARVIS_ASSERT(receiver->msg_queue->blocked_senders_tail == nullptr);

    Scheduler::remove_task(*sender);
    Scheduler::remove_task(*receiver);
    delete sender;
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all IPC unit tests with the test framework.
// Input: None
// Expect: All ipc_* tests are registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
// Runmode: kernel
// Testidea: Verifies that IPC::send() wakes a BLOCKED destination task (bug
// #014).
// When a task is blocked on its own queue (e.g. waiting for a reply in
// send_sync)
// and another task sends it a message, the blocked task must be set to READY.
// Input: Create receiver, block it on its own queue. Send it a message.
// Expect: Receiver transitions from BLOCKED to READY after the send.
JARVIS_TEST(ipc_send_wakes_blocked_destination) {
    auto* receiver = TaskControlBlock::create([]() {}, 5, 10);
    JARVIS_ASSERT(receiver != nullptr);
    JARVIS_ASSERT(receiver->msg_queue != nullptr);
    Scheduler::add_task(*receiver);

    // Manually block the receiver task on its own queue
    Scheduler::set_current(*receiver);
    receiver->state = TaskState::BLOCKED;

    // Send a message to the blocked receiver
    Message msg{};
    msg.sender_id = 1;
    msg.type = 42;
    msg.priority = 0;
    msg.data_size = 0;
    bool sent = IPC::send(receiver->id, msg, 0);
    JARVIS_ASSERT(sent);

    // Receiver should now be READY
    JARVIS_ASSERT(receiver->state == TaskState::READY);

    // Cleanup
    Scheduler::remove_task(*receiver);
    receiver->cleanup();
    delete receiver;
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all IPC unit tests with the test framework.
// Input: None
// Expect: All ipc_* tests are registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
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
    JARVIS_REGISTER_TEST(ipc_block_sender_adds_to_list);
    JARVIS_REGISTER_TEST(ipc_wake_sender_removes_from_list);
    JARVIS_REGISTER_TEST(ipc_wake_sender_terminated);
    JARVIS_REGISTER_TEST(ipc_wake_sender_restores_priority);
    JARVIS_REGISTER_TEST(ipc_send_block_full);
    JARVIS_REGISTER_TEST(ipc_send_sync_roundtrip);
    JARVIS_REGISTER_TEST(ipc_sender_unblocked_on_receiver_exit);
    JARVIS_REGISTER_TEST(ipc_send_wakes_blocked_destination);
}
