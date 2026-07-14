#pragma once

/*
 * Jarvis RTOS — Kernel Log Consumer Task
 */

/// @file dmesg_task.hpp
/// @brief Kernel log consumer task that drains the dmesg ring buffer to UART.

#pragma once

namespace kernel::task {

void dmesg_task_main();

} // namespace kernel::task