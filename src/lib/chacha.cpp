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

#include <chacha.hpp>

namespace crypto {

void chacha20_block(const uint8_t key[CHACHA_KEY_SIZE], uint32_t counter,
    const uint8_t nonce[CHACHA_NONCE_SIZE], uint8_t output[CHACHA_BLOCK_SIZE]) {

    ChaChaState state;
    chacha_init_state(state, key, counter, nonce);

    ChaChaState working = state;

    // 20 rounds = 10 double rounds
    for (int round = 0; round < 10; ++round) {
        // Column rounds
        chacha_quarter_round(working.s[0], working.s[4], working.s[8],
            working.s[12]);
        chacha_quarter_round(working.s[1], working.s[5], working.s[9],
            working.s[13]);
        chacha_quarter_round(working.s[2], working.s[6], working.s[10],
            working.s[14]);
        chacha_quarter_round(working.s[3], working.s[7], working.s[11],
            working.s[15]);

        // Diagonal rounds
        chacha_quarter_round(working.s[0], working.s[5], working.s[10],
            working.s[15]);
        chacha_quarter_round(working.s[1], working.s[6], working.s[11],
            working.s[12]);
        chacha_quarter_round(working.s[2], working.s[7], working.s[8],
            working.s[13]);
        chacha_quarter_round(working.s[3], working.s[4], working.s[9],
            working.s[14]);
    }

    // Add original state to working state
    for (size_t i = 0; i < CHACHA_STATE_WORDS; ++i) {
        working.s[i] += state.s[i];
    }

    // Serialize to little-endian bytes
    for (size_t i = 0; i < CHACHA_STATE_WORDS; ++i) {
        output[4 * i]     = static_cast<uint8_t>(working.s[i]);
        output[4 * i + 1] = static_cast<uint8_t>(working.s[i] >> 8);
        output[4 * i + 2] = static_cast<uint8_t>(working.s[i] >> 16);
        output[4 * i + 3] = static_cast<uint8_t>(working.s[i] >> 24);
    }
}

} // namespace crypto
