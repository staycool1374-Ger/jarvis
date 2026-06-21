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

#include <crc32.hpp>

namespace kernel {

uint32_t CRC32::table_[256];
bool CRC32::initialized_ = false;

void CRC32::init() {
    if (initialized_) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ POLY;
            } else {
                crc >>= 1;
            }
        }
        table_[i] = crc;
    }
    initialized_ = true;
}

uint32_t CRC32::update(uint32_t crc, const uint8_t* data, uint64_t len) {
    for (uint64_t i = 0; i < len; ++i) {
        crc = table_[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t CRC32::finalize(uint32_t crc) {
    return crc ^ XOR_OUT;
}

}