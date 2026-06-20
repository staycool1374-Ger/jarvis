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

/// @brief ECX bit 30 of leaf 1: RDRAND instruction supported.
inline constexpr uint32_t CPUID_ECX1_RDRAND = 1u << 30;

/// @brief EBX bit 18 of leaf 7 / subleaf 0: RDSEED instruction supported.
inline constexpr uint32_t CPUID_EBX7_RDSEED = 1u << 18;

/// @brief Check whether the RDRAND instruction is supported.
inline bool has_rdrand() {
    return (cpuid(1).ecx & CPUID_ECX1_RDRAND) != 0;
}

/// @brief Check whether the RDSEED instruction is supported.
inline bool has_rdseed() {
    return (cpuid(7, 0).ebx & CPUID_EBX7_RDSEED) != 0;
}

} // namespace arch
