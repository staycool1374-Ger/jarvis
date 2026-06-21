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

namespace kernel {

class CRC32 {
public:
    static void init();
    static uint32_t update(uint32_t crc, const uint8_t* data, uint64_t len);
    static uint32_t finalize(uint32_t crc);

    static constexpr uint32_t INITIAL = 0xFFFFFFFF;
    static constexpr uint32_t XOR_OUT = 0xFFFFFFFF;

private:
    static constexpr uint32_t POLY = 0xEDB88320;
    static uint32_t table_[256];
    static bool initialized_;
};

}