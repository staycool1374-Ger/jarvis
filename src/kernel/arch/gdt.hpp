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

/// @file gdt.hpp
/// @brief Global Descriptor Table and Task State Segment definitions.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief GDT descriptor structure for the LGDT instruction.
struct GDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/// @brief A single 64-bit GDT entry (8 bytes).
struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/// @brief Task State Segment structure for privilege-level 0 stack switching.
struct TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

/// @brief Manages the Global Descriptor Table and TSS.
class GDT {
public:
    GDT() = default;

    /// @brief Initialises the GDT entries (code, data, user, TSS).
    static void init();
    /// @brief Loads the GDT using the LGDT instruction.
    static void load();

    /// @brief Sets the RSP0 field in the TSS (kernel stack on ring 0 entry).
    /// @param rsp The stack pointer to use when entering ring 0.
    static void set_tss_rsp0(uint64_t rsp);

private:
    static constexpr size_t NUM_ENTRIES = 7;
    static GDTEntry entries_[NUM_ENTRIES];
    static TSS tss_;
    static GDTDescriptor desc_;
};

/// @brief GDT segment selector constants.
enum {
    GDT_NULL  = 0x00,
    GDT_CODE  = 0x08,
    GDT_DATA  = 0x10,
    GDT_USER_CODE = 0x18,
    GDT_USER_DATA = 0x20,
    GDT_TSS   = 0x28,
};

} // namespace arch
