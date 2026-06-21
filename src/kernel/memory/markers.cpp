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

#include <types.hpp>

namespace kernel {
namespace integrity {

static constexpr uint64_t make_marker(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                                       uint8_t e, uint8_t f, uint8_t g, uint8_t h) {
    return static_cast<uint64_t>(a)        |
           (static_cast<uint64_t>(b) << 8)  |
           (static_cast<uint64_t>(c) << 16) |
           (static_cast<uint64_t>(d) << 24) |
           (static_cast<uint64_t>(e) << 32) |
           (static_cast<uint64_t>(f) << 40) |
           (static_cast<uint64_t>(g) << 48) |
           (static_cast<uint64_t>(h) << 56);
}

__attribute__((section(".marker.text.start"), used))
extern const uint64_t _m_text_start = make_marker('T','X','T','_','S','T','R','T');

__attribute__((section(".marker.text.end"), used))
extern const uint64_t _m_text_end   = make_marker('T','X','T','_','E','N','D','_');

__attribute__((section(".marker.rodata.start"), used))
extern const uint64_t _m_rodata_start = make_marker('R','D','T','_','S','T','R','T');

__attribute__((section(".marker.rodata.end"), used))
extern const uint64_t _m_rodata_end   = make_marker('R','D','T','_','E','N','D','_');

__attribute__((section(".marker.data.start"), used))
extern const uint64_t _m_data_start = make_marker('D','T','A','_','S','T','R','T');

__attribute__((section(".marker.data.end"), used))
extern const uint64_t _m_data_end   = make_marker('D','T','A','_','E','N','D','_');

__attribute__((section(".marker.bss.start"), used))
extern const uint64_t _m_bss_start = make_marker('B','S','S','_','S','T','R','T');

__attribute__((section(".marker.bss.end"), used))
extern const uint64_t _m_bss_end   = make_marker('B','S','S','_','E','N','D','_');

__attribute__((section(".marker.stack.before"), used))
extern const uint64_t _m_stack_before = make_marker('S','T','K','_','B','F','R','_');

__attribute__((section(".marker.stack.after"), used))
extern const uint64_t _m_stack_after  = make_marker('S','T','K','_','A','F','T','_');

}
}