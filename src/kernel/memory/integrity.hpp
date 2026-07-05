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
namespace integrity {

// NOLINTBEGIN(bugprone-dynamic-static-initializers)
extern const uint64_t _m_text_start;
extern const uint64_t _m_text_end;
extern const uint64_t _m_rodata_start;
extern const uint64_t _m_rodata_end;
extern const uint64_t _m_data_start;
extern const uint64_t _m_data_end;
extern const uint64_t _m_bss_start;
extern const uint64_t _m_bss_end;
extern const uint64_t _m_stack_before;
extern const uint64_t _m_stack_after;

extern "C" {
    extern uint64_t _text_start[];
    extern uint64_t _text_end[];
    extern uint64_t _expected_code_crc[];
}
// NOLINTEND(bugprone-dynamic-static-initializers)

bool check_section_markers();
void reset_crc_state();
bool crc_process_chunk();
void idle_task_main();

}
}