#pragma once

#include <types.hpp>

namespace arch {

inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t msr_low = static_cast<uint32_t>(value);
    uint32_t msr_high = static_cast<uint32_t>(value >> 32);
    asm volatile("wrmsr" : : "c"(msr), "a"(msr_low), "d"(msr_high));
}

inline uint64_t rdmsr(uint32_t msr) {
    uint32_t msr_low, msr_high;
    asm volatile("rdmsr" : "=a"(msr_low), "=d"(msr_high) : "c"(msr));
    return (static_cast<uint64_t>(msr_high) << 32) | msr_low;
}

inline constexpr uint32_t IA32_STAR  = 0xC0000081;
inline constexpr uint32_t IA32_LSTAR = 0xC0000082;
inline constexpr uint32_t IA32_FMASK = 0xC0000084;

} // namespace arch
