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
    static constexpr size_t MAX_WAITERS = 32;

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
