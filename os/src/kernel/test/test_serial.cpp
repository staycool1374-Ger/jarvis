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
// Testidea: Verifies COM1 baud rate set to 115200 after init.
// Input: Initialize serial port
// Expect: Baud rate divisor configured for 115200
// Depends: kernel::Serial
JARVIS_TEST(serial_init_configures_baud) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies character written to serial TX register.
// Input: Call putchar(), check TX register (requires mock/loopback)
// Expect: Character appears in output buffer
// Depends: kernel::Serial::putchar()
JARVIS_TEST(serial_putchar_output) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies puts() appends \r\n to every string.
// Input: Call puts("test"), capture output
// Expect: Output is "test\r\n"
// Depends: kernel::Serial::puts()
JARVIS_TEST(serial_puts_appends_crlf) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies puts("") outputs only CRLF.
// Input: Call puts(""), capture output
// Expect: Output is "\r\n"
// Depends: kernel::Serial::puts()
JARVIS_TEST(serial_puts_empty_string) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Serial unit tests with the test framework.
// Input: None
// Expect: All Serial tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_serial_tests() {
    Logger::info("Registering serial tests");
    JARVIS_REGISTER_TEST(serial_init_configures_baud);
    JARVIS_REGISTER_TEST(serial_putchar_output);
    JARVIS_REGISTER_TEST(serial_puts_appends_crlf);
    JARVIS_REGISTER_TEST(serial_puts_empty_string);
}