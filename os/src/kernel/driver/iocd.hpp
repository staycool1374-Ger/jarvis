#pragma once

#include <types.hpp>

namespace kernel {
namespace iocd {

static constexpr uint64_t IOCD_REGISTER_DEVICE = 200;
static constexpr uint64_t IOCD_IRQ_EVENT       = 201;
static constexpr uint64_t IOCD_MMIO_MAP        = 202;
static constexpr uint64_t IOCD_MMIO_UNMAP      = 203;
static constexpr uint64_t IOCD_KEYBOARD_READ   = 204;
static constexpr uint64_t IOCD_SERIAL_READ     = 205;
static constexpr uint64_t IOCD_SERIAL_WRITE    = 206;

struct Msg {
    uint64_t type;
    uint64_t args[7];
};

struct Reply {
    int64_t result;
    uint64_t data[4];
};

/// @brief Record the PID of the IOCD (I/O Controller Daemon) task.
void set_iocd_pid(uint64_t pid);
/// @brief Get the recorded IOCD PID.
/// @return The PID, or 0 if not yet set.
uint64_t get_iocd_pid();
/// @brief Check if the current task is the IOCD.
/// @return true if the current task's PID matches the IOCD PID.
bool is_iocd_task();

} // namespace iocd
} // namespace kernel
