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

/// @file rand.hpp
/// @brief Hardware random number generation instructions (RDRAND, RDSEED).

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Read a 64-bit random value using the RDRAND instruction.
/// @param[out] value Filled with a random value on success.
/// @return true if a random value was obtained (carry flag set).
inline bool rdrand64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdrand %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

/// @brief Read a 64-bit random value using the RDSEED instruction.
/// @param[out] value Filled with a random value on success.
/// @return true if a random value was obtained (carry flag set).
inline bool rdseed64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdseed %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

} // namespace arch
