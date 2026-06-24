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

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel {
namespace sync {

static constexpr size_t QUEUE_MAX_MSG_SIZE = 32;
static constexpr size_t QUEUE_MAX_MSG_COUNT = 32;

struct QueueMessage {
    uint8_t data[QUEUE_MAX_MSG_SIZE];
    size_t  size;
};

class Queue {
public:
    static constexpr size_t MAX_WAITERS = CONFIG_SYNC_MAX_WAITERS;

    Queue() : head_(0), tail_(0), count_(0), send_waiters_(0), recv_waiters_(0
        ) {}
    /// @brief Initialize the message queue to empty.
    void init();

    /// @brief Send a message, blocking if the queue is full.
    /// @return true on success.
    bool send(const uint8_t* data, size_t size);
    /// @brief Send a message without blocking.
    /// @return true if the message was enqueued.
    bool try_send(const uint8_t* data, size_t size);
    /// @brief Receive a message, blocking if the queue is empty.
    /// @param[out] buf Destination buffer.
    /// @param[out] size Receives the message size.
    /// @return true on success.
    bool receive(uint8_t* buf, size_t* size);
    /// @brief Receive a message without blocking.
    /// @return true if a message was dequeued.
    bool try_receive(uint8_t* buf, size_t* size);
    size_t available() const { return count_; }

private:
    SpinLock lock_;
    QueueMessage msgs_[QUEUE_MAX_MSG_COUNT];
    size_t head_;
    size_t tail_;
    size_t count_;

    TaskControlBlock* send_waiters_[MAX_WAITERS];
    size_t send_waiters_count_;

    TaskControlBlock* recv_waiters_[MAX_WAITERS];
    size_t recv_waiters_count_;

    bool is_full() const { return count_ >= QUEUE_MAX_MSG_COUNT; }
    bool is_empty() const { return count_ == 0; }

    bool add_send_waiter(TaskControlBlock* task);
    bool add_recv_waiter(TaskControlBlock* task);
    void wake_send_one();
    void wake_recv_one();
};

}
}
