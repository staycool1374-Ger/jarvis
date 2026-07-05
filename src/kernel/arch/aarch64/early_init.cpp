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

/// @file early_init.cpp
/// @brief Stub file — early AArch64 init is performed in boot.S assembly.

// Early init is now done in boot.S (assembly).
// This file exists only to keep the build system from complaining
// about a missing .cpp file in the aarch64 arch directory.
struct EarlyInitStub {
    EarlyInitStub() {}
};
static EarlyInitStub stub;
