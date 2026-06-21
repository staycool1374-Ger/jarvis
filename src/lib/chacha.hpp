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

/// @file chacha.hpp
/// @brief ChaCha20 stream cipher core (RFC 8439 variant).
///
/// 20 rounds, 256-bit key, 96-bit nonce, 32-bit counter.
/// Block output is 64 bytes.

#pragma once

#include <types.hpp>

namespace crypto {

/// @brief ChaCha20 block size in bytes.
inline constexpr size_t CHACHA_BLOCK_SIZE = 64;

/// @brief ChaCha20 key size in bytes (256 bits).
inline constexpr size_t CHACHA_KEY_SIZE = 32;

/// @brief ChaCha20 nonce size in bytes (96 bits).
inline constexpr size_t CHACHA_NONCE_SIZE = 12;

/// @brief Number of 32-bit words in the ChaCha20 state matrix.
inline constexpr size_t CHACHA_STATE_WORDS = 16;

/// @brief ChaCha20 internal state (16 × uint32_t).
struct ChaChaState {
    uint32_t s[CHACHA_STATE_WORDS];
};

/// @brief ChaCha20 quarter round (operates on four state words).
inline void chacha_quarter_round(uint32_t& a, uint32_t& b, uint32_t& c,
    uint32_t& d) {
    a += b; d ^= a; d = (d << 16) | (d >> 16);
    c += d; b ^= c; b = (b << 12) | (b >> 20);
    a += b; d ^= a; d = (d << 8) | (d >> 24);
    c += d; b ^= c; b = (b << 7) | (b >> 25);
}

/// @brief Initialise the ChaCha20 state matrix with key, counter and nonce.
/// @param[out] state   Initialised state matrix.
/// @param[in]  key     256-bit key (32 bytes).
/// @param[in]  counter Initial block counter.
/// @param[in]  nonce   96-bit nonce (12 bytes).
inline void chacha_init_state(ChaChaState& state, const uint8_t key[CHACHA_KEY_SIZE],
    uint32_t counter, const uint8_t nonce[CHACHA_NONCE_SIZE]) {
    // Constants: "expand 32-byte k"
    state.s[0] = 0x61707865;
    state.s[1] = 0x3320646e;
    state.s[2] = 0x79622d32;
    state.s[3] = 0x6b206574;

    // Key (8 × uint32_t, little-endian)
    for (size_t i = 0; i < 8; ++i) {
        state.s[4 + i] = static_cast<uint32_t>(key[4 * i])
            | (static_cast<uint32_t>(key[4 * i + 1]) << 8)
            | (static_cast<uint32_t>(key[4 * i + 2]) << 16)
            | (static_cast<uint32_t>(key[4 * i + 3]) << 24);
    }

    // Block counter (uint32_t)
    state.s[12] = counter;

    // Nonce (3 × uint32_t, little-endian)
    for (size_t i = 0; i < 3; ++i) {
        state.s[13 + i] = static_cast<uint32_t>(nonce[4 * i])
            | (static_cast<uint32_t>(nonce[4 * i + 1]) << 8)
            | (static_cast<uint32_t>(nonce[4 * i + 2]) << 16)
            | (static_cast<uint32_t>(nonce[4 * i + 3]) << 24);
    }
}

/// @brief Generate a single ChaCha20 block (64 bytes).
/// @param[in]  key     256-bit key.
/// @param[in]  counter Block counter.
/// @param[in]  nonce   96-bit nonce.
/// @param[out] output  64-byte output buffer.
void chacha20_block(const uint8_t key[CHACHA_KEY_SIZE], uint32_t counter,
    const uint8_t nonce[CHACHA_NONCE_SIZE], uint8_t output[CHACHA_BLOCK_SIZE]);

} // namespace crypto
