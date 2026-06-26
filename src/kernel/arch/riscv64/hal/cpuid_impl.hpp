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

inline bool has_rdrand() { return false; }
inline bool has_rdseed() { return false; }

inline bool has_fpu() {
    uint64_t misa;
    asm volatile("csrr %0, misa" : "=r"(misa));
    return (misa & (1ULL << ('F' - 'A'))) != 0;
}

} // namespace arch
