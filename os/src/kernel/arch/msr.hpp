#pragma once

#include <types.hpp>

namespace arch {

inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = static_cast<uint32_t>(value);
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    asm volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline constexpr uint32_t IA32_STAR  = 0xC0000081;
inline constexpr uint32_t IA32_LSTAR = 0xC0000082;
inline constexpr uint32_t IA32_FMASK = 0xC0000084;

} // namespace arch
