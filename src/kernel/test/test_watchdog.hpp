#pragma once

#include <types.hpp>

namespace kernel::test {

extern uint64_t g_watchdog_deadline;
extern uint64_t g_watchdog_task_id;

void watchdog_arm(uint64_t ms, const char* test_name);
void watchdog_disarm();
[[noreturn]] void watchdog_panic();

} // namespace kernel::test
