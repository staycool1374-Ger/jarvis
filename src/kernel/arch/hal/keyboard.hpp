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

/// @file keyboard.hpp
/// @brief PS/2 keyboard driver (x86_64) / no-op stubs (AArch64, RISC-V).

#pragma once

#include <types.hpp>
#include <kernel/sync/spsc_ring.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

/// @cond
#if defined(CONFIG_ARCH_X86_64)
/// @endcond

/// @brief PS/2 keyboard driver with modifier tracking and a character ring
/// buffer.
class Keyboard {
  public:
    Keyboard() = default;
    /// @brief Initialise the PS/2 keyboard controller.
    static void init();
    /// @brief Handle a keyboard interrupt — read scancode and enqueue
    /// character.
    static void handle_irq();
    /// @brief Dequeue a single character (non-blocking).
    /// @param[out] c Output character.
    /// @return true if a character was available.
    static bool getchar(char &c);
    /// @brief Read multiple characters into a buffer (non-blocking).
    /// @param buf Output buffer.
    /// @param len Maximum number of characters to read.
    /// @return Number of characters actually read.
    static size_t read(char *buf, size_t len);
    /// @brief Discard all buffered characters.
    static void flush();
    /// @return true if Shift is held.
    static bool is_shifted() {
        return shift_;
    }
    /// @return true if Ctrl is held.
    static bool is_ctrl() {
        return ctrl_;
    }
    /// @return true if Alt is held.
    static bool is_alt() {
        return alt_;
    }

  private:
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint16_t DATA_PORT = 0x60;
    static constexpr uint16_t STATUS_PORT = 0x64;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static SPSCRing<char, RING_SIZE> ring_;
    static constinit bool shift_;
    static constinit bool ctrl_;
    static constinit bool alt_;
    static constinit bool caps_;
    /// @brief Push a character into the ring buffer.
    static bool push_ring(char c);
    /// @brief Update Shift/Ctrl/Alt/Caps state from a scancode.
    static void update_modifiers(uint8_t scancode, bool pressed);
};

/// @cond
#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)

/// @brief No-op keyboard stub for architectures without PS/2.
class Keyboard {
  public:
    static void init() {
    }
    static void handle_irq() {
    }
    static bool getchar(char &) {
        return false;
    }
    static size_t read(char *, size_t) {
        return 0;
    }
    static void flush() {
    }
    static bool is_shifted() {
        return false;
    }
    static bool is_ctrl() {
        return false;
    }
    static bool is_alt() {
        return false;
    }
};

#else
#error "HAL: no keyboard implementation for this architecture"
#endif
/// @endcond

} // namespace arch
