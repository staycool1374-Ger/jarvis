#pragma once

#include <types.hpp>

namespace kernel::ipc {

enum BootMessage : uint32_t {
    MSG_DAEMON_READY = 0xF0000001,
    MSG_DAEMON_FAILED = 0xF0000002,
};

} // namespace kernel::ipc
