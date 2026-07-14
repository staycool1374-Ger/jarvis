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

/// @file random.hpp
/// @brief Kernel random number generator (RDRAND/RDSEED + ChaCha20 CSPRNG).

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Initialises the kernel RNG.
///
/// Detects available hardware RNG (RDSEED > RDRAND) and seeds the ChaCha20
/// CSPRNG.  Falls back to RDTSC jitter if no hardware RNG is available.
void random_init();

/// @brief Fills a buffer with cryptographically secure random bytes.
/// @param buffer  Destination buffer.
/// @param length  Number of bytes to write.
void random_fill(uint8_t *buffer, size_t length);

/// @brief Returns a single 64-bit random value.
inline uint64_t random_u64() {
    uint64_t val{};
    random_fill(reinterpret_cast<uint8_t *>(&val), sizeof(val));
    return val;
}

/// @brief Returns a single 32-bit random value.
inline uint32_t random_u32() {
    uint32_t val{};
    random_fill(reinterpret_cast<uint8_t *>(&val), sizeof(val));
    return val;
}

} // namespace kernel
