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

void set_iocd_pid(uint64_t pid);
uint64_t get_iocd_pid();
bool is_iocd_task();

} // namespace iocd
} // namespace kernel
