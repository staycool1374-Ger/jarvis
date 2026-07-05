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
/// @brief RISC-V64 hardware random-number stubs — no x86 RDRAND/RDSEED
///        equivalents; always return false.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Stub for x86 RDRAND — always fails on RISC-V.
/// @param[out] out Unmodified output parameter.
/// @return false (no hardware random number generator).
inline bool rdrand64(uint64_t& out) {
    (void)out;
    return false;
}

/// @brief Stub for x86 RDSEED — always fails on RISC-V.
/// @param[out] out Unmodified output parameter.
/// @return false (no hardware random number generator).
inline bool rdseed64(uint64_t& out) {
    (void)out;
    return false;
}

} // namespace arch
