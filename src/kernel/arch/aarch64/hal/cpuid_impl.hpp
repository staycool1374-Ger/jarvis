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

/// @file cpuid_impl.hpp
/// @brief AArch64 CPU feature detection via ID registers.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief AArch64 CPUID result structure (kept for x86 API compatibility).
struct CpuIdResult {
    uint64_t eax, ebx, ecx, edx;
};

/// @brief AArch64 CPUID stub — returns zeroed result (no x86 CPUID on AArch64).
/// @param[in] leaf Requested CPUID leaf (ignored).
/// @return Zero-initialised CpuIdResult.
inline CpuIdResult cpuid(uint32_t leaf) {
    (void)leaf;
    return {};
}

/// @brief Check for RDRAND support (always false on AArch64).
/// @return false.
inline bool has_rdrand() { return false; }
/// @brief Check for RDSEED support (always false on AArch64).
/// @return false.
inline bool has_rdseed() { return false; }

/// @brief Check for FP and Advanced SIMD support via ID_AA64PFR0_EL1.
/// @return true if both FP and Advanced SIMD are implemented.
inline bool has_fpu() {
    uint64_t id_aa64pfr0;
    asm volatile("mrs %0, id_aa64pfr0_el1" : "=r"(id_aa64pfr0));
    uint64_t fp = (id_aa64pfr0 >> 16) & 0xF;
    uint64_t advsimd = (id_aa64pfr0 >> 20) & 0xF;
    return fp != 0xF && advsimd != 0xF;
}

} // namespace arch
