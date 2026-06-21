/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file ipc.hpp
/// @brief Priority-based IPC — send, recv, events, notifications.

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/sync/spinlock_guard.hpp>

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

    sync::SpinLock lock_;  ///< Protects queue + blocked_senders + owner state

    /// @brief Initialize the message queue to empty.
    void init();
    /// @brief Push a message into the queue (priority-ordered insertion).
    /// @return true on success, false if the queue is full.
    bool push(const Message& msg);
    /// @brief Pop the highest-priority message from the queue.
    /// @return true if a message was dequeued.
    bool pop(Message& msg);

    bool is_empty() const { return count == 0; }
    bool is_full() const  { return count >= IPC_MAX_QUEUE_MSG; }

    /// @brief Returns the highest priority with messages, or
    /// IPC_PRIORITY_LEVELS if empty.
    size_t highest_priority() const;
};

/// @brief Inter-process communication manager.
class IPC {
public:
    /// @brief Initialize the IPC subsystem (per-task message queues).
    static void init();

    /// @brief Sends a message to a destination task.
    static bool send(uint64_t dest_id, const Message& msg, uint64_t flags = 0);

    /// @brief Receives a message from the calling task's own queue.
    static bool recv(Message& msg);

    /// @brief Sends and blocks until a reply arrives.
    static bool send_sync(uint64_t dest_id, const Message& msg, Message& reply);

    /// @brief Returns the message queue for a given task ID.
    static MessageQueue& queue(uint64_t task_id);

    /// @brief Blocks the current task on a full queue
    /// (may boost owner priority).
    static bool block_sender(MessageQueue& q, TaskControlBlock& task);

    /// @brief Wakes the oldest blocked sender and
    /// restores owner priority.
    static void wake_sender(MessageQueue& q, TaskControlBlock& receiver);
};

} // namespace kernel
