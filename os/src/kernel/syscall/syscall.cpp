#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>

namespace kernel {

void Syscall::init() {
}

uint64_t Syscall::handle(uint64_t number, uint64_t arg0, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3)
{
    switch (static_cast<SyscallNumber>(number)) {
    case SyscallNumber::YIELD:
        arch::io_wait();
        return 0;

    case SyscallNumber::SEND: {
        Message msg{};
        msg.sender_id = arg0;
        msg.type = arg2;
        msg.data_size = arg3;
        auto* data_ptr = reinterpret_cast<uint8_t*>(arg1);
        for (size_t i = 0; i < arg3 && i < IPC_MAX_MSG_SIZE; ++i) {
            msg.data[i] = data_ptr[i];
        }
        return IPC::send(arg0, msg) ? 0 : 1;
    }

    case SyscallNumber::RECEIVE:
        return 0;

    case SyscallNumber::PRINT:
        return 0;

    case SyscallNumber::GET_TICKS:
        return arch::Timer::ticks();

    case SyscallNumber::EXIT:
        while (true) asm volatile("hlt");

    case SyscallNumber::CREATE_MAILBOX: {
        auto* mb = IPC::create_mailbox(arg0);
        return mb ? reinterpret_cast<uint64_t>(mb) : 0;
    }

    case SyscallNumber::DESTROY_MAILBOX:
        IPC::destroy_mailbox(arg0);
        return 0;

    default:
        return static_cast<uint64_t>(-1);
    }
}

} // namespace kernel
