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
/// @brief RISC-V64 CPU identification — reads MISA CSR for FPU detection;
///        stub for x86-derived CPUID leaves and RNG feature checks.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Stub CPUID result (x86 compat; no real CPUID on RISC-V).
struct CpuIdResult {
    uint64_t eax, ebx, ecx, edx;
};

/// @brief Stub CPUID instruction (always returns zeroed result).
/// @param leaf CPUID leaf (ignored).
/// @return Zeroed CpuIdResult.
inline CpuIdResult cpuid(uint32_t leaf) {
    (void)leaf;
    return {};
}

/// @brief Check for hardware RDRAND support (always false on RISC-V).
/// @return false.
inline bool has_rdrand() { return false; }

/// @brief Check for hardware RDSEED support (always false on RISC-V).
/// @return false.
inline bool has_rdseed() { return false; }

/// @brief Detect FPU presence by checking the F extension bit in MISA.
/// @return true if the F extension is present.
inline bool has_fpu() {
    uint64_t misa;
    asm volatile("csrr %0, misa" : "=r"(misa));
    return (misa & (1ULL << ('F' - 'A'))) != 0;
}

} // namespace arch
