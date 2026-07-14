#pragma once

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

/// @file serial.hpp
/// @brief Serial port (UART) driver — architecture-agnostic interface.

#pragma once

namespace arch {

/// @brief Serial port (UART) driver — architecture-agnostic interface.
class Serial {
  public:
    /// @brief Initialise the serial port hardware.
    static void init();
    /// @brief Transmit a single character.
    /// @param c Character to transmit.
    static void putchar(char c);
    /// @brief Receive a single character (blocking).
    /// @return The received character.
    static char getchar();
    /// @brief Transmit a null-terminated string.
    /// @param str Null-terminated string to transmit.
    static void puts(const char *str);
};

} // namespace arch
