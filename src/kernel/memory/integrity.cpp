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

#include <kernel/memory/integrity.hpp>
#include <crc32.hpp>
#include <logger.hpp>
#include <assert.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

namespace kernel {
namespace integrity {

static constexpr uint64_t CRC_CHUNK_SIZE = 4096;

static uint32_t crc_accumulator = CRC32::INITIAL;
static uint64_t crc_offset = 0;
static uint64_t crc_total_len = 0;
static bool crc_complete = false;
static bool crc_checked = false;

extern "C" {
    extern uint64_t _text_start[];
    extern uint64_t _text_end[];
    extern uint64_t _expected_code_crc[];
}

/// @brief Verify section markers — compares each linker-placed magic constant
///        against its expected ASCII tag.  Fails via Logger on mismatch.
/// @return true if all markers match.
void check_section_markers() {
    if (_m_text_start      != 0x545254535F545854ULL) { panic("INTEGRITY: .text start marker corrupted"); }
    if (_m_text_end        != 0x5F444E455F545854ULL) { panic("INTEGRITY: .text end marker corrupted");   }
    if (_m_rodata_start    != 0x545254535F544452ULL) { panic("INTEGRITY: .rodata start marker corrupted"); }
    if (_m_rodata_end      != 0x5F444E455F544452ULL) { panic("INTEGRITY: .rodata end marker corrupted");   }
    if (_m_data_start      != 0x545254535F415444ULL) { panic("INTEGRITY: .data start marker corrupted");   }
    if (_m_data_end        != 0x5F444E455F415444ULL) { panic("INTEGRITY: .data end marker corrupted");     }
    if (_m_bss_start       != 0x545254535F535342ULL) { panic("INTEGRITY: .bss start marker corrupted");    }
    if (_m_bss_end         != 0x5F444E455F535342ULL) { panic("INTEGRITY: .bss end marker corrupted");      }
    if (_m_stack_before    != 0x5F5246425F4B5453ULL) { panic("INTEGRITY: stack before marker corrupted");  }
    if (_m_stack_after     != 0x5F5446415F4B5453ULL) { panic("INTEGRITY: stack after marker corrupted");   }
}

/// @brief Reset CRC accumulator, offset, and completion flags for a fresh scan.
void reset_crc_state() {
    crc_accumulator = CRC32::INITIAL;
    crc_offset = 0;
    crc_complete = false;
    crc_checked = false;

    uint64_t start_addr = reinterpret_cast<uint64_t>(_text_start) + 8;
    uint64_t end_addr   = reinterpret_cast<uint64_t>(_text_end);
    if (end_addr > start_addr) {
        crc_total_len = end_addr - start_addr;
    } else {
        crc_total_len = 0;
    }
}

/// @brief Process the next 4 KiB chunk of the .text CRC scan.
///        When the scan completes, compares the computed CRC against the
///        expected value stored by the linker.  Panics on mismatch.
/// @return true if the CRC scan is complete (or was already checked).
bool crc_process_chunk() {
    if (crc_complete) {
        if (!crc_checked) {
            crc_checked = true;
            uint32_t expected = _expected_code_crc[0];
            uint32_t computed = CRC32::finalize(crc_accumulator);
            if (computed != expected) {
                panic("CODE CRC MISMATCH - system compromised");
            } else {
                Logger::info("Idle: code CRC verified OK");
            }
        }
        return true;
    }

    uint64_t start_addr = reinterpret_cast<uint64_t>(_text_start) + 8;

    uint64_t remaining = crc_total_len - crc_offset;
    uint64_t chunk = (remaining > CRC_CHUNK_SIZE) ? CRC_CHUNK_SIZE : remaining;

    if (chunk == 0) {
        crc_complete = true;
        return true;
    }

    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(start_addr + crc_offset);
    crc_accumulator = CRC32::update(crc_accumulator, data, chunk);
    crc_offset += chunk;

    if (crc_offset >= crc_total_len) {
        crc_complete = true;
    }

    return crc_complete;
}

/// @brief Idle task entry point — continuously verifies section markers and
///        processes code CRC chunks between HLT instructions.
void idle_task_main() {
    static bool inited = false;
    if (!inited) {
        CRC32::init();
        reset_crc_state();
        inited = true;
    }
    for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) {
        check_section_markers();
        crc_process_chunk();
        arch::hlt();
    }
}

}
}