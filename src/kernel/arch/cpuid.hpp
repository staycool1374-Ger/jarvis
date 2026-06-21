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

/// @file cpuid.hpp
/// @brief CPUID instruction wrapper and feature flag constants.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief CPUID leaf output (EAX, EBX, ECX, EDX).
struct CpuIdResult {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

/// @brief Execute the CPUID instruction.
/// @param leaf   CPUID leaf number (EAX input).
/// @param subleaf Sub-leaf number (ECX input, default 0).
/// @return The four output registers.
inline CpuIdResult cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuIdResult r;
    asm volatile("cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf));
    return r;
}

// ── Feature bit constants ──

// Leaf 1 EDX
inline constexpr uint32_t CPUID_EDX1_FPU      = 1u << 0;
inline constexpr uint32_t CPUID_EDX1_FXSR     = 1u << 24;
inline constexpr uint32_t CPUID_EDX1_SSE      = 1u << 25;
inline constexpr uint32_t CPUID_EDX1_SSE2     = 1u << 26;

// Leaf 1 ECX
inline constexpr uint32_t CPUID_ECX1_SSE3     = 1u << 0;
inline constexpr uint32_t CPUID_ECX1_SSSE3    = 1u << 9;
inline constexpr uint32_t CPUID_ECX1_SSE4_1   = 1u << 19;
inline constexpr uint32_t CPUID_ECX1_SSE4_2   = 1u << 20;
inline constexpr uint32_t CPUID_ECX1_RDRAND   = 1u << 30;

// Leaf 7 / subleaf 0 EBX
inline constexpr uint32_t CPUID_EBX7_RDSEED   = 1u << 18;

/// @brief Check whether the x87 FPU is supported.
inline bool has_fpu() {
    return (cpuid(1).edx & CPUID_EDX1_FPU) != 0;
}

/// @brief Check whether FXSAVE/FXRSTOR is supported.
inline bool has_fxsr() {
    return (cpuid(1).edx & CPUID_EDX1_FXSR) != 0;
}

/// @brief Check whether SSE is supported.
inline bool has_sse() {
    return (cpuid(1).edx & CPUID_EDX1_SSE) != 0;
}

/// @brief Check whether the RDRAND instruction is supported.
inline bool has_rdrand() {
    return (cpuid(1).ecx & CPUID_ECX1_RDRAND) != 0;
}

/// @brief Check whether the RDSEED instruction is supported.
inline bool has_rdseed() {
    return (cpuid(7, 0).ebx & CPUID_EBX7_RDSEED) != 0;
}

} // namespace arch
