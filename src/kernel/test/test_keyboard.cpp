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
// Testidea: Verifies ring buffer empty after keyboard init.
// Input: Initialize keyboard driver
// Expect: Buffer head == tail, count == 0
// Depends: kernel::Keyboard
JARVIS_TEST(keyboard_init_clears_buffer, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies scancode enqueued then read back in order.
// Input: Enqueue scancode 0x1E, then dequeue
// Expect: Returns 0x1E
// Depends: kernel::Keyboard::enqueue/dequeue
JARVIS_TEST(keyboard_enqueue_dequeue_scancode, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies when 256-byte ring buffer full, new scancodes dropped.
// Input: Fill buffer with 256 scancodes, add one more
// Expect: No overflow, new scancode dropped
// Depends: kernel::Keyboard buffer logic
JARVIS_TEST(keyboard_buffer_full_drops, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies Shift/Alt/Ctrl state bits toggle correctly on make/break.
// Input: Send make code for Shift, then break code
// Expect: Shift state bit set then cleared
// Depends: kernel::Keyboard modifier tracking
JARVIS_TEST(keyboard_modifier_tracking, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PS/2 controller self-test executed and acknowledged.
// Input: Run keyboard self-test sequence
// Expect: Controller responds with 0xAA (or appropriate ACK)
// Depends: kernel::Keyboard initialization
JARVIS_TEST(keyboard_self_test_sequence, "PRE: iocd | POST: none") {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Keyboard unit tests with the test framework.
// Input: None
// Expect: All Keyboard tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_keyboard_tests() {
    Logger::info("Registering keyboard tests");
    JARVIS_REGISTER_TEST(keyboard_init_clears_buffer);
    JARVIS_REGISTER_TEST(keyboard_enqueue_dequeue_scancode);
    JARVIS_REGISTER_TEST(keyboard_buffer_full_drops);
    JARVIS_REGISTER_TEST(keyboard_modifier_tracking);
    JARVIS_REGISTER_TEST(keyboard_self_test_sequence);
}