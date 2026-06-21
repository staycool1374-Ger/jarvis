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

#pragma once

#include <types.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

namespace arch {

class Serial {
public:
    /// @brief Initialize COM1 serial port (115200 baud, 8N1).
    static void init() {
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
    static void putchar(char c) {
        if (c == '\n') {
            while ((inb(arch::COM1 + 5) & 0x20) == 0);
            outb(arch::COM1, '\r');
        }
        while ((inb(arch::COM1 + 5) & 0x20) == 0);
        outb(arch::COM1, c);
    }

    /// @brief Write a null-terminated string to the serial port.
    static void puts(const char* s) {
        while (*s) putchar(*s++);
    }
};

} // namespace arch
