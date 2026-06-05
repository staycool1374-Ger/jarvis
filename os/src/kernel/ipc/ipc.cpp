#include <kernel/ipc/ipc.hpp>
#include <kernel/task/scheduler.hpp>
#include <assert.hpp>
#include <string.hpp>

namespace kernel {

Mailbox IPC::mailboxes_[MAX_MAILBOXES] = {};
size_t IPC::mailbox_count_ = 0;

void IPC::init() {
    mailbox_count_ = 0;
    for (size_t i = 0; i < MAX_MAILBOXES; ++i) {
        mailboxes_[i].waiting_sender = nullptr;
        mailboxes_[i].waiting_receiver = nullptr;
    }
}

Mailbox* IPC::create_mailbox(uint64_t owner_id) {
    if (mailbox_count_ >= MAX_MAILBOXES) return nullptr;
    auto& mb = mailboxes_[mailbox_count_++];
    mb.head = 0;
    mb.tail = 0;
    mb.count = 0;
    mb.owner_id = owner_id;
    mb.waiting_sender = nullptr;
    mb.waiting_receiver = nullptr;
    return &mb;
}

void IPC::destroy_mailbox(uint64_t owner_id) {
    for (size_t i = 0; i < mailbox_count_; ++i) {
        if (mailboxes_[i].owner_id == owner_id) {
            if (mailboxes_[i].waiting_sender) {
                mailboxes_[i].waiting_sender->state = TaskState::READY;
            }
            if (mailboxes_[i].waiting_receiver) {
                mailboxes_[i].waiting_receiver->state = TaskState::READY;
            }
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
    if (!mb) return false;

    if (mb->is_full()) return false;

    memcpy(&mb->messages[mb->tail], &msg, sizeof(Message));
    mb->tail = (mb->tail + 1) % IPC_MAX_MAILBOX_MSG;
    ++mb->count;

    if (mb->waiting_receiver) {
        mb->waiting_receiver->state = TaskState::READY;
        mb->waiting_receiver = nullptr;
    }

    return true;
}

bool IPC::receive(uint64_t src_id, Message& msg) {
    auto* mb = find_mailbox(src_id);
    if (!mb) return false;

    if (mb->is_empty()) return false;

    memcpy(&msg, &mb->messages[mb->head], sizeof(Message));
    mb->head = (mb->head + 1) % IPC_MAX_MAILBOX_MSG;
    --mb->count;

    if (mb->waiting_sender) {
        mb->waiting_sender->state = TaskState::READY;
        mb->waiting_sender = nullptr;
    }

    return true;
}

bool IPC::send_sync(uint64_t dest_id, const Message& msg, Message& reply) {
    auto* mb = find_mailbox(dest_id);
    if (!mb) return false;

    if (mb->is_full()) {
        auto* task = Scheduler::current_task();
        if (!task) return false;
        mb->waiting_sender = task;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }

    if (!send(dest_id, msg)) return false;

    while (true) {
        if (receive(dest_id, reply)) return true;
        auto* task = Scheduler::current_task();
        if (!task) return false;
        mb->waiting_receiver = task;
        task->state = TaskState::BLOCKED;
        Scheduler::reschedule();
    }
}

} // namespace kernel
