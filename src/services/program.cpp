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

#include <services/program.hpp>

namespace service {

const ProgramRegistry::Program* ProgramRegistry::programs_[MAX_PROGRAMS] = {};
size_t ProgramRegistry::count_ = 0;

void ProgramRegistry::init() {
    count_ = 0;
}

void ProgramRegistry::register_program(const char* name, const char* desc, void (*entry)()) {
    if (count_ >= MAX_PROGRAMS) return;
    auto* prog = new Program{name, desc, entry};
    if (!prog) return;
    programs_[count_++] = prog;
}

const ProgramRegistry::Program* ProgramRegistry::find(const char* name) {
    for (size_t i = 0; i < count_; ++i) {
        if (programs_[i]->name) {
            const char* a = programs_[i]->name;
            const char* b = name;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (*a == '\0' && *b == '\0') return programs_[i];
        }
    }
    return nullptr;
}

} // namespace service
