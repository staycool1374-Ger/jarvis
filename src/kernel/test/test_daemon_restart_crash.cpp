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

/// @file test_daemon_restart_crash.cpp
/// @brief Demonstrates the daemon restart crash after reload_daemon_tasks.
///
/// When restart_stale_daemons() is called after reload_daemon_tasks(),
/// the newly created daemon tasks crash on their first context switch
/// (no startup message printed).  This is a latent bug in the test
/// isolation teardown sequence: reload_daemon_tasks() leaves the kernel
/// in a state (corrupted page tables or MemPool) that makes userspace
/// ELF execution unsafe.
///
/// The crash is observable in the serial output:
///   [DAEMON] 'vfsd' restarted (PID=4, restart #1)   ← created
///   Scheduler: task '' (ID=4) terminated              ← crashes
///   [DAEMON] 'vfsd' died (PID=4), restart_count=1     ← notify_death in cleanup
///
/// The dmesg_push_base(0xDA01) in notify_death persists the event
/// across reboot_from_table() because the dmesg buffer is never cleared.

#include <test.hpp>
#include <kernel/test/test_isolate.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Restart daemons after reload to expose the post-cleanup crash
// Input: reload_daemon_tasks then restart_stale_daemons
// Expect: New daemon tasks die silently on first context switch
// Depends: kernel::daemon, kernel::Scheduler, kernel::test::reload_daemon_tasks
JARVIS_TEST(daemon_restart_after_cleanup_crash, "PRE: vfsd,iocd") {
    // Kill all tasks including daemons
    test::reload_daemon_tasks();

    // Recreate daemon tasks from initrd — these crash at first run
    daemon::restart_stale_daemons();

    // After restart_stale_daemons, the daemon tasks exist in the
    // scheduler but will crash when the timer ISR picks them.
    // Check they are at least present in the daemon entries.
    for (uint64_t i = 0; i < daemon::MAX_DAEMONS; ++i) {
        const auto& entry = daemon::get_entry(i);
        if (entry.pid == 0) continue;
        auto* t = Scheduler::find_task(entry.pid);
        JARVIS_ASSERT(t != nullptr);
    }

    JARVIS_TEST_PASS();
}
