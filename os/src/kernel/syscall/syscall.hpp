/// @file syscall.hpp
/// @brief System call interface and dispatcher.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief System call numbers recognised by the kernel.
enum class SyscallNumber : uint64_t {
    YIELD       = 0,
    SEND        = 1,
    RECEIVE     = 2,
    SEND_SYNC   = 3,
    PRINT       = 4,
    GET_TICKS   = 5,
    EXIT        = 6,
    CREATE_MAILBOX = 7,
    DESTROY_MAILBOX = 8,
};

/// @brief System call handler and dispatcher.
class Syscall {
public:
    /// @brief Initialises the syscall handler (MSR setup).
    static void init();
    /// @brief Dispatches a system call to the appropriate handler.
    /// @param number The syscall number.
    /// @param arg0   Argument 0.
    /// @param arg1   Argument 1.
    /// @param arg2   Argument 2.
    /// @param arg3   Argument 3.
    /// @return Return value from the syscall handler.
    static uint64_t handle(uint64_t number, uint64_t arg0, uint64_t arg1,
                           uint64_t arg2, uint64_t arg3);
};

} // namespace kernel
