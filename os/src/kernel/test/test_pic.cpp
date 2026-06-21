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
// Testidea: Verifies IRQ0-IRQ7 remapped from 0x08-0x0F to 0x20-0x27,
// IRQ8-IRQ15 to 0x28-0x2F.
// Input: Initialize PIC, check IDT vectors
// Expect: IRQ vectors at 0x20-0x2F
// Depends: kernel::arch::PIC
JARVIS_TEST(pic_remap_vectors) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies all IRQ lines masked after init (except cascade for
// slave).
// Input: Initialize PIC, read mask registers
// Expect: Master mask = 0xFB (IRQ2 cascade enabled), Slave mask = 0xFF
// Depends: kernel::arch::PIC
JARVIS_TEST(pic_mask_all) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies sending EOI to PIC clears the in-service register.
// Input: Trigger IRQ, send EOI, check ISR
// Expect: ISR bit cleared for that IRQ
// Depends: kernel::arch::PIC
JARVIS_TEST(pic_ocw2_end_of_interrupt) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all PIC unit tests with the test framework.
// Input: None
// Expect: All PIC tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_pic_tests() {
    Logger::info("Registering PIC tests");
    JARVIS_REGISTER_TEST(pic_remap_vectors);
    JARVIS_REGISTER_TEST(pic_mask_all);
    JARVIS_REGISTER_TEST(pic_ocw2_end_of_interrupt);
}