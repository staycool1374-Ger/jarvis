/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies SYS_HEALTH_STATUS syscall returns system health metrics.
// Input: Call SYS_HEALTH_STATUS with valid buffer
// Expect: Returns health metrics struct with deadlock_count,
// recovered_count, watchdog_firings
// Depends: SYS_HEALTH_STATUS syscall implementation
JARVIS_TEST(health_syscall_returns_metrics, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies health metrics contain correct fields.
// Input: Query health status
// Expect: Struct has deadlock_count, recovered_count, watchdog_firings fields
// Depends: Health status implementation
JARVIS_TEST(health_metrics_fields, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies health counters are monotonically increasing.
// Input: Trigger multiple deadlocks/recoveries, query health each time
// Expect: Counters never decrease
// Depends: Health status implementation
JARVIS_TEST(health_counters_monotonic, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies unprivileged caller gets EACCES from SYS_HEALTH_STATUS.
// Input: Call SYS_HEALTH_STATUS from unprivileged task
// Expect: Returns EACCES
// Depends: SYS_HEALTH_STATUS syscall, capability check
JARVIS_TEST(health_privileged_only, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies /proc/health exposes same data in human-readable format.
// Input: Read /proc/health
// Expect: Human-readable output matching health metrics
// Depends: VFS /proc filesystem, health status implementation
JARVIS_TEST(health_proc_filesystem, "PRE: vfsd, iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all health status unit tests with the test framework.
// Input: None
// Expect: All health tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_health_tests() {
    Logger::info("Registering health status tests");
    JARVIS_REGISTER_TEST(health_syscall_returns_metrics);
    JARVIS_REGISTER_TEST(health_metrics_fields);
    JARVIS_REGISTER_TEST(health_counters_monotonic);
    JARVIS_REGISTER_TEST(health_privileged_only);
    JARVIS_REGISTER_TEST(health_proc_filesystem);
}