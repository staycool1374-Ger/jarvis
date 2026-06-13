/// @file ipc.hpp
/// @brief Priority-based IPC — send, recv, events, notifications.

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {

/// @brief Flags for IPC::send().
enum IpcFlags : uint64_t {
    IPC_NONBLOCK = 1 << 0,
};

/// @brief Priority-ordered circular message queue embedded via pointer in each TCB.
struct MessageQueue {
    Message  msgs[IPC_MAX_QUEUE_MSG];
    uint64_t prio_bitmap;  ///< Bit n set = at least one msg at priority n
    size_t   head;
    size_t   tail;
    size_t   count;

    TaskControlBlock* blocked_senders_head;
    TaskControlBlock* blocked_senders_tail;

    /// @brief TCB that owns this queue (set during task creation).
    TaskControlBlock* owner;

    MessageQueue()
        : prio_bitmap(0)
        , head(0)
        , tail(0)
        , count(0)
        , blocked_senders_head(nullptr)
        , blocked_senders_tail(nullptr)
        , owner(nullptr)
        {}

    void init();
    bool push(const Message& msg);
    bool pop(Message& msg);

    bool is_empty() const { return count == 0; }
    bool is_full() const  { return count >= IPC_MAX_QUEUE_MSG; }

    /// @brief Returns the highest priority with messages, or IPC_PRIORITY_LEVELS if empty.
    size_t highest_priority() const;
};

/// @brief Inter-process communication manager.
class IPC {
public:
    static void init();

    /// @brief Sends a message to a destination task.
    static bool send(uint64_t dest_id, const Message& msg, uint64_t flags = 0);

    /// @brief Receives a message from the calling task's own queue.
    static bool recv(Message& msg);

    /// @brief Sends and blocks until a reply arrives.
    static bool send_sync(uint64_t dest_id, const Message& msg, Message& reply);

    /// @brief Returns the message queue for a given task ID.
    static MessageQueue& queue(uint64_t task_id);

    /// @brief Blocks the current task on a full queue (may boost owner priority).
    static bool block_sender(MessageQueue& q, TaskControlBlock& task);

    /// @brief Wakes the oldest blocked sender and restores owner priority.
    static void wake_sender(MessageQueue& q, TaskControlBlock& receiver);
};

} // namespace kernel
