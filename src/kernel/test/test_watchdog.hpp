#pragma once

/// @file test_watchdog.hpp
/// @brief Watchdog timer helpers for test timeout safety.

#include <types.hpp>

namespace kernel::test {

extern constinit uint64_t g_watchdog_deadline_ns;
extern constinit uint64_t g_watchdog_task_id;

/// @brief Arm the watchdog with both TSC-based and PIT-based timers.
///        PIT channel 1 one-shot (~55ms) + TSC deadline.
void watchdog_arm(uint64_t ms, const char* test_name);
void watchdog_disarm();
[[noreturn]] void watchdog_panic();

/// @brief Synchronous check — uses TSC when available, falls back to
///        PIT channel 0 elapsed-time accumulation when TSC freq is 0.
///        Also reloads PIT channel 1 one-shot.
void watchdog_check_inline();

} // namespace kernel::test
