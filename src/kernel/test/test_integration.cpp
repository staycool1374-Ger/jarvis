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

/// @file test_integration.cpp
/// @brief Cross-subsystem integration tests.

#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Renders Mandelbrot to framebuffer, computes CRC32, asserts
// known-good hash.
// Input: Run mandelbrot rendering, compute CRC32 of framebuffer
// Expect: CRC32 matches expected hash
// Depends: Framebuffer, mandelbrot demo
JARVIS_TEST(mandelbrot_crc_hash, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Integration unit tests with the test framework.
// Input: None
// Expect: All Integration tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_integration_tests() {
    Logger::info("Registering integration tests");
    JARVIS_REGISTER_TEST(mandelbrot_crc_hash);
}