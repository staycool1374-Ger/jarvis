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

/// @file serial.cpp
/// @brief Serial (UART 16550) driver — provides early kernel debug output over COM1.

#include <kernel/arch/serial.hpp>
#include <kernel/arch/hal/io.hpp>
#include <constants.hpp>

namespace arch {

/// @brief Initialise the serial port (COM1) at 115200 baud, 8N1.
void Serial::init() {
    outb(arch::COM1 + 1, 0x00);
    outb(arch::COM1 + 3, 0x80);
    outb(arch::COM1 + 0, 0x01);
    outb(arch::COM1 + 1, 0x00);
    outb(arch::COM1 + 3, 0x03);
    outb(arch::COM1 + 2, 0xC7);
    outb(arch::COM1 + 4, 0x0B);
    outb(arch::COM1 + 4, 0x0F);
}

/// @brief Write a single character to the serial port.
/// Automatically expands '\\n' to "\\r\\n" for carriage-return line-feed.
/// @param c Character to transmit.
void Serial::putchar(char c) {
    if (c == '\n') {
        while ((inb(arch::COM1 + 5) & 0x20) == 0);
        outb(arch::COM1, '\r');
    }
    while ((inb(arch::COM1 + 5) & 0x20) == 0);
    outb(arch::COM1, c);
}

/// @brief Read a single character from the serial port (blocking).
/// @return The received character.
char Serial::getchar() {
    while ((inb(arch::COM1 + 5) & 0x01) == 0);
    return static_cast<char>(inb(arch::COM1));
}

/// @brief Write a null-terminated string to the serial port.
/// @param s Null-terminated string to transmit.
void Serial::puts(const char* s) {
    while (*s) putchar(*s++);
}

} // namespace arch
