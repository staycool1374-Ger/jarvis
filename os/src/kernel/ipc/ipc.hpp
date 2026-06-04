/// @file ipc.hpp
/// @brief Inter-process communication via mailboxes.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Maximum payload size in bytes for an IPC message.
static constexpr size_t IPC_MAX_MSG_SIZE = 64;
/// @brief Maximum number of messages a mailbox can hold.
static constexpr size_t IPC_MAX_MAILBOX_MSG = 16;

/// @brief A single IPC message with sender ID, type, and payload.
struct Message {
    uint64_t sender_id;
    uint64_t type;
    uint8_t  data[IPC_MAX_MSG_SIZE];
    size_t   data_size;
};

/// @brief A mailbox holding a circular buffer of messages.
struct Mailbox {
    Message  messages[IPC_MAX_MAILBOX_MSG];
    size_t   head;
    size_t   tail;
    size_t   count;
    uint64_t owner_id;

    bool is_empty() const { return count == 0; }
    bool is_full() const { return count >= IPC_MAX_MAILBOX_MSG; }
};

/// @brief Inter-process communication manager (send/receive via mailboxes).
class IPC {
public:
    /// @brief Initialises the IPC subsystem.
    static void init();

    /// @brief Creates a mailbox for a given owner task.
    /// @param owner_id Task ID of the mailbox owner.
    /// @return Pointer to the new mailbox, or nullptr.
    static Mailbox* create_mailbox(uint64_t owner_id);
    /// @brief Destroys the mailbox owned by a task.
    /// @param owner_id Task ID of the owner.
    static void destroy_mailbox(uint64_t owner_id);

    /// @brief Sends an asynchronous message to a destination task.
    /// @param dest_id Target task ID.
    /// @param msg     Message to send.
    /// @return True on success.
    static bool send(uint64_t dest_id, const Message& msg);
    /// @brief Receives a message from a source task (non-blocking).
    /// @param src_id Source task ID.
    /// @param msg    Reference to store the received message.
    /// @return True if a message was available.
    static bool receive(uint64_t src_id, Message& msg);
    /// @brief Sends a message and waits for a reply synchronously.
    /// @param dest_id Target task ID.
    /// @param msg     Message to send.
    /// @param reply   Reference to store the reply message.
    /// @return True on success.
    static bool send_sync(uint64_t dest_id, const Message& msg, Message& reply);

    /// @brief Finds a mailbox by owner task ID.
    /// @param owner_id Task ID to search for.
    /// @return Pointer to the mailbox, or nullptr.
    static Mailbox* find_mailbox(uint64_t owner_id);

private:
    static constexpr size_t MAX_MAILBOXES = 64;
    static Mailbox mailboxes_[MAX_MAILBOXES];
    static size_t mailbox_count_;
};

} // namespace kernel
