#pragma once

#include <types.hpp>

namespace arch {

struct CpuIdResult {
    uint64_t eax, ebx, ecx, edx;
};

inline CpuIdResult cpuid(uint32_t leaf) {
    (void)leaf;
    return {};
}

inline bool has_rdrand() { return true; }
inline bool has_rdseed() { return true; }

inline bool has_fpu() {
    uint64_t id_aa64pfr0;
    asm volatile("mrs %0, id_aa64pfr0_el1" : "=r"(id_aa64pfr0));
    uint64_t fp = (id_aa64pfr0 >> 16) & 0xF;
    uint64_t advsimd = (id_aa64pfr0 >> 20) & 0xF;
    return fp != 0xF && advsimd != 0xF;
}

} // namespace arch
