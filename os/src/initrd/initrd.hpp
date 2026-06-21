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

namespace initrd {

struct InitrdFile {
    const uint8_t* data;
    uint64_t size;
};

struct InitrdEntry {
    char name[256];
    uint64_t size;
    bool is_dir;
};

void init(const uint8_t* start, const uint8_t* end);
InitrdFile find(const char* path);

bool readdir(uint64_t* pos, InitrdEntry* entry);

} // namespace initrd
