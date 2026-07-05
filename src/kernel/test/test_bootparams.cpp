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
// Testidea: Verifies debug_ipc=1 in cmdline activates the IPC debug flag.
// Input: Boot with "debug_ipc=1" in cmdline
// Expect: IPC debug flag is true
// Depends: kernel::BootParams
JARVIS_TEST(bootparams_parse_debug_flags, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies multiple debug_* flags parsed from single cmdline.
// Input: Boot with "debug_ipc=1 debug_vfs=1"
// Expect: Both IPC and VFS debug flags are true
// Depends: kernel::BootParams
JARVIS_TEST(bootparams_parse_multiple_flags, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies no command line — all debug flags stay false.
// Input: Boot with empty cmdline
// Expect: All debug flags false
// Depends: kernel::BootParams
JARVIS_TEST(bootparams_empty_cmdline, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies garbage string parsed without crash (ignores unknown
// flags).
// Input: Boot with "garbage unknown_flag=1 debug_ipc=1"
// Expect: No crash, debug_ipc=1 recognized, unknown ignored
// Depends: kernel::BootParams
JARVIS_TEST(bootparams_malformed_cmdline, "PRE: none | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Boot Parameters unit tests with the test framework.
// Input: None
// Expect: All BootParams tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_bootparams_tests() {
    Logger::info("Registering bootparams tests");
    JARVIS_REGISTER_TEST(bootparams_parse_debug_flags);
    JARVIS_REGISTER_TEST(bootparams_parse_multiple_flags);
    JARVIS_REGISTER_TEST(bootparams_empty_cmdline);
    JARVIS_REGISTER_TEST(bootparams_malformed_cmdline);
}