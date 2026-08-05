/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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

/// @file qemu_debugcon.hpp
/// @brief Ultra-low-latency debug output over the QEMU debugcon ISA port
///        (0xE9).  The kernel logging backends (Logger, debug_write, the
///        IPC/scheduler trace kit) route through this class so serialisation
///        cannot warp microsecond-scale scheduling/IPC timing.
///
/// Why this exists (H2 deferred-switch investigation):
///   A UART 16550 write blocks on the LSR transmit-holding-register-empty
///   bit — ~87us per byte at 115200 baud — so a handful of trace bytes can
///   cost tens of microseconds, measurably perturbing scheduler/ISR timing
///   (e.g. the `[RS]`/`[SW]`/`[APPLY]` traces that masked or exposed the H2
///   race).  The QEMU "debugcon" device forwards bytes written to I/O port
///   0xE9 directly to the host backend with no FIFO/baud pacing: a single
///   `outb`, single-digit nanoseconds per character, lock-free, no
///   interrupts.
///
/// Usage: launch QEMU with a debugcon backend, e.g.
///   qemu-system-x86_64 ... -serial mon:stdio -debugcon stdio
/// (writes to 0xE9 without a backend are silently dropped — harmless).

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Minimal, lock-free writer to the QEMU debugcon device (I/O port
///        0xE9).  On non-x86_64 targets every method is a no-op so the class
///        can be called unconditionally from shared logging code.
class QemuDebugcon {
public:
    /// @brief QEMU debugcon ISA I/O port (the "magic" 0xE9 port).
    static constexpr uint16_t DEBUGCON_PORT = 0xE9;

    /// @brief Write a single character directly to the QEMU hypervisor.
    ///        Lock-free, no interrupt interaction, single-digit nanoseconds.
    static inline void putc(char c) noexcept {
#if defined(CONFIG_ARCH_X86_64)
        asm volatile("outb %0, %1"
                     :
                     : "a"(static_cast<uint8_t>(c)), "Nd"(DEBUGCON_PORT));
#else
        (void)c;
#endif
    }

    /// @brief Write a NUL-terminated string to the host debug log.
    static inline void write(const char *str) noexcept {
        if (!str)
            return;
        while (*str)
            putc(*str++);
    }

    /// @brief Write a bounded byte buffer to the host debug log.
    static inline void write(const char *data, size_t size) noexcept {
        if (!data)
            return;
        for (size_t i = 0; i < size; ++i)
            putc(data[i]);
    }
};

} // namespace arch
