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

/// @file test_isolate.hpp
/// @brief Test isolation snapshot/restore helpers.

#pragma once

#include <types.hpp>
#include <kernel/test/resource_tracker.hpp>

namespace kernel::test {

bool snapshot_create();
void snapshot_restore(const char* test_name = nullptr);
void snapshot_destroy();

/// @brief Terminate old daemon tasks and reload them from initrd.
///        Call AFTER snapshot_restore + snapshot_destroy to replace
///        corrupted page tables with fresh ones from initrd.
void reload_daemon_tasks();

/// @brief Run all tests from the generated registry, calling setup/test/teardown
///        in sequence with snapshot isolation between each test.
void run_all_isolated_tests();

/// @brief Tracks whether any VFS syscall was invoked during the current test.
/// snapshot_restore() checks this flag to decide whether daemon tasks need
/// a full reload (VFS touched) or can be left running (VFS not touched).
extern bool g_vfs_touched; // NOLINT(bugprone-dynamic-static-initializers)

/// @brief Called by VFS syscall handlers to mark that VFS state was touched.
void mark_vfs_touched();

} // namespace kernel::test
