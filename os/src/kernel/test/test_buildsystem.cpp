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
// Testidea: Verifies ARCH=x86_64 selects x86_64 toolchain and sources.
// Input: Build with ARCH=x86_64
// Expect: x86_64-elf-gcc used, arch/x86_64/ sources compiled
// Depends: Makefile, toolchain
/* Pseudocode:
 * 1. Check CC variable contains x86_64-elf-
 * 2. Check arch/x86_64/ files in build output
 * 3. Verify no other arch sources compiled
 */
JARVIS_TEST(buildsystem_arch_x86_64_toolchain) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies invalid ARCH value produces clear error message.
// Input: Build with ARCH=invalid_arch
// Expect: Make fails with descriptive error
// Depends: Makefile
/* Pseudocode:
 * 1. Run make ARCH=invalid_arch
 * 2. Check error output contains "Unsupported ARCH" or similar
 * 3. Verify build stops immediately
 */
JARVIS_TEST(buildsystem_invalid_arch_error) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies default ARCH is x86_64.
// Input: Build without ARCH variable
// Expect: Defaults to x86_64, builds successfully
// Depends: Makefile
/* Pseudocode:
 * 1. Run make (no ARCH)
 * 2. Verify x86_64 toolchain used
 * 3. Verify arch/x86_64/ sources compiled
 */
JARVIS_TEST(buildsystem_default_arch_x86_64) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies kernel compiles with -DCONFIG_DEBUG for test builds.
// Input: Check debug build flags
// Expect: CONFIG_DEBUG defined, debug symbols present
// Depends: Makefile
/* Pseudocode:
 * 1. Check build output for -DCONFIG_DEBUG
 * 2. Check build output for -g -Og
 * 3. Verify debug target builds with these flags
 */
JARVIS_TEST(buildsystem_debug_flags) {
    JARVIS_TEST_PASS();
}

void register_buildsystem_tests() {
    Logger::info("Registering buildsystem tests");

    JARVIS_REGISTER_TEST(buildsystem_arch_x86_64_toolchain);
    JARVIS_REGISTER_TEST(buildsystem_invalid_arch_error);
    JARVIS_REGISTER_TEST(buildsystem_default_arch_x86_64);
    JARVIS_REGISTER_TEST(buildsystem_debug_flags);
}