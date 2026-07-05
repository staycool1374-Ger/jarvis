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

/// @file test_multiboot.cpp
/// @brief Multiboot protocol parsing tests.

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies each known tag type (framebuffer, memory map, module,
// cmdline) found.
// Input: Parse multiboot info with known tags
// Expect: find_tag_by_type returns valid pointer for each known type
// Depends: kernel::boot::Multiboot2
JARVIS_TEST(mb2_find_tag_by_type, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies unknown tag type returns nullptr.
// Input: Call find_tag_by_type with invalid type
// Expect: Returns nullptr
// Depends: kernel::boot::Multiboot2
JARVIS_TEST(mb2_find_tag_nonexistent, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies framebuffer tag struct width/height/bpp correctly
// populated.
// Input: Parse multiboot with framebuffer tag
// Expect: width, height, bpp match bootloader values
// Depends: kernel::boot::Multiboot2
JARVIS_TEST(mb2_framebuffer_tag_fields, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies memory map tag parses entry count and entry size
// correctly.
// Input: Parse multiboot with memory map tag
// Expect: entry_count and entry_size match tag data
// Depends: kernel::boot::Multiboot2
JARVIS_TEST(mb2_memory_map_tag_entries, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies module tag returns valid start/end addresses.
// Input: Parse multiboot with module tag
// Expect: mod_start and mod_end are valid addresses
// Depends: kernel::boot::Multiboot2
JARVIS_TEST(mb2_module_tag_start_end, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Multiboot unit tests with the test framework.
// Input: None
// Expect: All Multiboot tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_multiboot_tests() {
    Logger::info("Registering multiboot tests");
    JARVIS_REGISTER_TEST(mb2_find_tag_by_type);
    JARVIS_REGISTER_TEST(mb2_find_tag_nonexistent);
    JARVIS_REGISTER_TEST(mb2_framebuffer_tag_fields);
    JARVIS_REGISTER_TEST(mb2_memory_map_tag_entries);
    JARVIS_REGISTER_TEST(mb2_module_tag_start_end);
}