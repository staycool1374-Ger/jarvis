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
/// @brief AArch64 random number generation via ARMv8.5 RNG system registers.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Read a 64-bit random value using the ARM RNG system register (s3_3_c2_c4_0).
/// @param[out] out Filled with a random 64-bit value.
/// @return Always true (the system register always provides a value).
/// @note Uses the ARMv8.5 RNG architectural feature via implementation-specific sysreg.
inline bool rdrand64(uint64_t& out) {
    uint64_t val;
    asm volatile("mrs %0, s3_3_c2_c4_0" : "=r"(val));
    out = val;
    return true;
}

/// @brief Read a 64-bit seed value using the ARM RNG system register (s3_3_c2_c4_1).
/// @param[out] out Filled with a random 64-bit seed.
/// @return Always true.
/// @note Analogous to x86 RDSEED; uses a different sysreg from rdrand64.
inline bool rdseed64(uint64_t& out) {
    uint64_t val;
    asm volatile("mrs %0, s3_3_c2_c4_1" : "=r"(val));
    out = val;
    return true;
}

} // namespace arch
