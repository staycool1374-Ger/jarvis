#include <kernel/ipc/ipc.hpp>
#include <assert.hpp>
#include <string.hpp>

namespace kernel {

Mailbox IPC::mailboxes_[MAX_MAILBOXES] = {};
size_t IPC::mailbox_count_ = 0;

void IPC::init() {
    mailbox_count_ = 0;
}

Mailbox* IPC::create_mailbox(uint64_t owner_id) {
    ASSERT(mailbox_count_ < MAX_MAILBOXES);
    auto& mb = mailboxes_[mailbox_count_++];
    mb.head = 0;
    mb.tail = 0;
    mb.count = 0;
    mb.owner_id = owner_id;
    return &mb;
}

void IPC::destroy_mailbox(uint64_t owner_id) {
    for (size_t i = 0; i < mailbox_count_; ++i) {
        if (mailboxes_[i].owner_id == owner_id) {
            mailboxes_[i] = mailboxes_[--mailbox_count_];
            return;
        }
    }
}

Mailbox* IPC::find_mailbox(uint64_t owner_id) {
    for (size_t i = 0; i < mailbox_count_; ++i) {
        if (mailboxes_[i].owner_id == owner_id) {
            return &mailboxes_[i];
        }
    }
    return nullptr;
}

bool IPC::send(uint64_t dest_id, const Message& msg) {
    auto* mb = find_mailbox(dest_id);
    if (!mb || mb->is_full()) return false;

    memcpy(&mb->messages[mb->tail], &msg, sizeof(Message));
    mb->tail = (mb->tail + 1) % IPC_MAX_MAILBOX_MSG;
    ++mb->count;
    return true;
}

bool IPC::receive(uint64_t src_id, Message& msg) {
    auto* mb = find_mailbox(src_id);
    if (!mb || mb->is_empty()) return false;

    memcpy(&msg, &mb->messages[mb->head], sizeof(Message));
    mb->head = (mb->head + 1) % IPC_MAX_MAILBOX_MSG;
    --mb->count;
    return true;
}

bool IPC::send_sync(uint64_t dest_id, const Message& msg, Message& reply) {
    if (!send(dest_id, msg)) return false;

    return receive(dest_id, reply);
}

} // namespace kernel
