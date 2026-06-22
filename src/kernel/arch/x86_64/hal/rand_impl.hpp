#pragma once

#include <types.hpp>

namespace arch {

inline bool rdrand64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdrand %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

inline bool rdseed64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdseed %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

} // namespace arch
