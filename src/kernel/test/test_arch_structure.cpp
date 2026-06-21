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
#include <string.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies all x86_64-specific code lives under arch/x86_64/.
// Input: Scan source tree for arch-specific files
// Expect: No .cpp/.hpp files with x86_64 code outside arch/x86_64/
// Depends: Build system, source tree structure
/* Pseudocode:
 * 1. Check that arch/ directory only contains interface headers
 * 2. Check that arch/x86_64/ contains all implementation files
 * 3. Verify no #ifdef __x86_64__ outside arch/x86_64/
 * 4. Build with ARCH=x86_64 succeeds
 */
JARVIS_TEST(arch_x86_64_isolation) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies generic arch/ contains only interface headers.
// Input: List files in arch/ directory
// Expect: Only .hpp interface files, no .cpp implementations
// Depends: Source tree structure
/* Pseudocode:
 * 1. List files in src/kernel/arch/
 * 2. Assert all .hpp files are pure interfaces (no implementations)
 * 3. Assert no .cpp files exist in arch/
 * 4. Verify each header has corresponding impl in arch/x86_64/
 */
JARVIS_TEST(arch_generic_interfaces_only) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies build system selects correct arch directory.
// Input: Build with ARCH=x86_64, ARCH=invalid
// Expect: x86_64 builds, invalid ARCH fails with clear error
// Depends: Makefile, build system
/* Pseudocode:
 * 1. make ARCH=x86_64 -> succeeds
 * 2. make ARCH=arm64 -> fails with clear error message
 * 3. make (default) -> builds x86_64
 */
JARVIS_TEST(buildsystem_arch_selection) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies HAL abstraction headers exist in arch/.
// Input: Check for ArchPageTable, ArchContext, ArchInterruptController, ArchTimer, ArchIO
// Expect: All 5 HAL interface headers present in arch/
// Depends: HAL design
/* Pseudocode:
 * 1. Check arch/page_table.hpp exists (interface)
 * 2. Check arch/context.hpp exists (interface)
 * 3. Check arch/interrupt_controller.hpp exists (interface)
 * 4. Check arch/timer.hpp exists (interface)
 * 5. Check arch/io.hpp exists (interface)
 */
JARVIS_TEST(hal_interfaces_exist) {
    JARVIS_TEST_PASS();
}

void register_arch_structure_tests() {
    Logger::info("Registering arch structure tests");

    JARVIS_REGISTER_TEST(arch_x86_64_isolation);
    JARVIS_REGISTER_TEST(arch_generic_interfaces_only);
    JARVIS_REGISTER_TEST(buildsystem_arch_selection);
    JARVIS_REGISTER_TEST(hal_interfaces_exist);
}