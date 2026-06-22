#pragma once

#include <types.hpp>

namespace arch {

struct CpuIdResult {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

inline CpuIdResult cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuIdResult r;
    asm volatile("cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf));
    return r;
}

inline constexpr uint32_t CPUID_EDX1_FPU      = 1u << 0;
inline constexpr uint32_t CPUID_EDX1_FXSR     = 1u << 24;
inline constexpr uint32_t CPUID_EDX1_SSE      = 1u << 25;
inline constexpr uint32_t CPUID_EDX1_SSE2     = 1u << 26;
inline constexpr uint32_t CPUID_ECX1_SSE3     = 1u << 0;
inline constexpr uint32_t CPUID_ECX1_SSSE3    = 1u << 9;
inline constexpr uint32_t CPUID_ECX1_SSE4_1   = 1u << 19;
inline constexpr uint32_t CPUID_ECX1_SSE4_2   = 1u << 20;
inline constexpr uint32_t CPUID_ECX1_RDRAND   = 1u << 30;
inline constexpr uint32_t CPUID_EBX7_RDSEED   = 1u << 18;

inline bool has_fpu() { return (cpuid(1).edx & CPUID_EDX1_FPU) != 0; }
inline bool has_fxsr() { return (cpuid(1).edx & CPUID_EDX1_FXSR) != 0; }
inline bool has_sse() { return (cpuid(1).edx & CPUID_EDX1_SSE) != 0; }
inline bool has_rdrand() { return (cpuid(1).ecx & CPUID_ECX1_RDRAND) != 0; }
inline bool has_rdseed() { return (cpuid(7, 0).ebx & CPUID_EBX7_RDSEED) != 0; }

} // namespace arch
