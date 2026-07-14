#pragma once

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
/// @brief Global Descriptor Table (x86_64) / no-op stubs for other
/// architectures.

#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

/// @cond
#if defined(CONFIG_ARCH_X86_64)
/// @endcond

/// @brief x86-64 GDT descriptor (6-byte packed, used with LGDT).
struct GDTDescriptor {
    uint16_t limit; ///< Size of the GDT minus one.
    uint64_t base;  ///< Linear address of the GDT.
} __attribute__((packed));

/// @brief x86-64 GDT entry (8-byte packed descriptor).
struct GDTEntry {
    uint16_t limit_low;  ///< Bits 0–15 of the segment limit.
    uint16_t base_low;   ///< Bits 0–15 of the base address.
    uint8_t base_mid;    ///< Bits 16–23 of the base address.
    uint8_t access;      ///< Access byte (present, DPL, type, etc.).
    uint8_t granularity; ///< Flags + bits 16–19 of the limit.
    uint8_t base_high;   ///< Bits 24–31 of the base address.
} __attribute__((packed));

/// @brief x86-64 Task-State Segment structure (used for IST and RSP0).
struct TSS {
    uint32_t reserved0;   ///< Reserved.
    uint64_t rsp0;        ///< Stack pointer for privilege level 0.
    uint64_t rsp1;        ///< Stack pointer for privilege level 1.
    uint64_t rsp2;        ///< Stack pointer for privilege level 2.
    uint64_t reserved1;   ///< Reserved.
    uint64_t ist1;        ///< Interrupt Stack Table entry 1.
    uint64_t ist2;        ///< Interrupt Stack Table entry 2.
    uint64_t ist3;        ///< Interrupt Stack Table entry 3.
    uint64_t ist4;        ///< Interrupt Stack Table entry 4.
    uint64_t ist5;        ///< Interrupt Stack Table entry 5.
    uint64_t ist6;        ///< Interrupt Stack Table entry 6.
    uint64_t ist7;        ///< Interrupt Stack Table entry 7.
    uint64_t reserved2;   ///< Reserved.
    uint16_t reserved3;   ///< Reserved.
    uint16_t iopb_offset; ///< I/O Permission Bitmap offset.
} __attribute__((packed));

/// @brief x86-64 Global Descriptor Table manager.
class GDT {
  public:
    GDT() = default;
    /// @brief Initialise the GDT with default segments and a TSS descriptor.
    static void init();
    /// @brief Load the GDT via the LGDT instruction.
    static void load();
    /// @brief Update RSP0 in the TSS (used on syscall entry).
    /// @param rsp New kernel stack pointer for ring 0.
    static void set_tss_rsp0(uint64_t rsp);

  private:
    static constexpr size_t NUM_ENTRIES = 7;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static GDTEntry entries_[NUM_ENTRIES];
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static TSS tss_;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static GDTDescriptor desc_;
};

/// @brief GDT segment selector constants.
enum : uint8_t {
    GDT_NULL = 0x00,      ///< Null descriptor selector.
    GDT_CODE = 0x08,      ///< Kernel code segment (ring 0).
    GDT_DATA = 0x10,      ///< Kernel data segment (ring 0).
    GDT_USER_CODE = 0x18, ///< User code segment (ring 3).
    GDT_USER_DATA = 0x20, ///< User data segment (ring 3).
    GDT_TSS = 0x28,       ///< TSS descriptor selector.
};

/// @cond
#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)

/// @brief No-op GDT stub for architectures without a GDT.
class GDT {
  public:
    static inline void init() {
    }
    static inline void load() {
    }
    static inline void set_tss_rsp0(uint64_t) {
    }
};

#else
#error "HAL: no gdt implementation for this architecture"
#endif
/// @endcond

} // namespace arch
