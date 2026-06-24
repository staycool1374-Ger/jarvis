#pragma once

#include <types.hpp>

namespace arch {

inline bool rdrand64(uint64_t& out) {
    uint64_t val;
    asm volatile("mrs %0, s3_3_c2_c4_0" : "=r"(val));
    out = val;
    return true;
}

inline bool rdseed64(uint64_t& out) {
    uint64_t val;
    asm volatile("mrs %0, s3_3_c2_c4_1" : "=r"(val));
    out = val;
    return true;
}

} // namespace arch
