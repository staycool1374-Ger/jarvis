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

/// @file test_cleanup.hpp
/// @brief Proper destructor-based cleanup after tests (alternative to snapshot restore).

#pragma once
#include <types.hpp>

namespace kernel::test {

/// @brief Clean up all kernel resources created during testing without using the
///        snapshot mechanism. Destructors are called on every resource object,
///        tasks are dequeued, and all memory is returned to pools.
///
/// After this call the kernel is in a clean post-init state (idle task only,
/// no drivers loaded, empty dmesg, fresh daemons from initrd).
void test_cleanup_all();

}

