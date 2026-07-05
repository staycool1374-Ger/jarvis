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

/// @file rand_impl.hpp
/// @brief x86_64 RDRAND and RDSEED instructions — hardware random number generation.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Execute the RDRAND instruction to obtain a 64-bit hardware random value.
/// @param[out] value Receives the random 64-bit value.
/// @return true if a random number was successfully generated, false if the CPU did not have
///         enough entropy.
inline bool rdrand64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdrand %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

/// @brief Execute the RDSEED instruction to obtain a 64-bit hardware seed value.
/// @param[out] value Receives the 64-bit seed.
/// @return true if a seed was successfully generated, false if entropy is insufficient.
inline bool rdseed64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdseed %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

} // namespace arch
