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

/// @file keyboard.hpp
/// @brief PS/2 keyboard driver with lock-free SPSC ring buffer and modifier tracking.

#pragma once

#include <types.hpp>
#include <kernel/sync/spsc_ring.hpp>

namespace arch {

/// @brief PS/2 keyboard driver with interrupt-driven character ring buffer.
/// @note Uses a lock-free SPSC ring for ISR-safe character delivery.
/// @note Tracks Shift, Ctrl, Alt, and Caps Lock modifier states.
class Keyboard {
public:
    Keyboard() = default;

    /// @brief Initialises the PS/2 keyboard controller.
    static void init();
    /// @brief Handles the keyboard IRQ (reads scancode from port).
    static void handle_irq();

    /// @brief Reads a single character from the ring buffer (non-blocking).
    /// @param c Reference to store the character.
    /// @return True if a character was available.
    static bool getchar(char& c);
    /// @brief Reads multiple characters from the ring buffer.
    /// @param buf Output buffer.
    /// @param len Maximum number of bytes to read.
    /// @return Number of bytes actually read.
    static size_t read(char* buf, size_t len);
    /// @brief Discards all buffered input.
    static void flush();

    /// @brief Checks if Shift is held.
    /// @return True if Shift is active.
    static bool is_shifted() { return shift_; }
    /// @brief Checks if Ctrl is held.
    /// @return True if Ctrl is active.
    static bool is_ctrl() { return ctrl_; }
    /// @brief Checks if Alt is held.
    /// @return True if Alt is active.
    static bool is_alt() { return alt_; }

private:
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint16_t DATA_PORT = 0x60;
    static constexpr uint16_t STATUS_PORT = 0x64;

    static SPSCRing<char, RING_SIZE> ring_;

    static bool shift_;
    static bool ctrl_;
    static bool alt_;
    static bool caps_;

    /// @brief Pushes a character into the ring buffer.
    /// @param c Character to push.
    /// @return True if the buffer was not full.
    static bool push_ring(char c);
    /// @brief Updates modifier state based on a scancode.
    /// @param scancode The raw scancode byte.
    /// @param pressed  True if key press, false if release.
    static void update_modifiers(uint8_t scancode, bool pressed);
};

} // namespace arch
