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

namespace arch {

/// @brief Write a value to a model-specific register (MSR).
inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t msr_low = static_cast<uint32_t>(value);
    uint32_t msr_high = static_cast<uint32_t>(value >> 32);
    asm volatile("wrmsr" : : "c"(msr), "a"(msr_low), "d"(msr_high));
}

/// @brief Read a value from a model-specific register (MSR).
/// @return The 64-bit MSR value.
inline uint64_t rdmsr(uint32_t msr) {
    uint32_t msr_low, msr_high;
    asm volatile("rdmsr" : "=a"(msr_low), "=d"(msr_high) : "c"(msr));
    return (static_cast<uint64_t>(msr_high) << 32) | msr_low;
}

inline constexpr uint32_t IA32_STAR  = 0xC0000081;
inline constexpr uint32_t IA32_LSTAR = 0xC0000082;
inline constexpr uint32_t IA32_FMASK = 0xC0000084;

} // namespace arch
