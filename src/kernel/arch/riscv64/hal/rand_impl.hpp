#pragma once

#include <types.hpp>

namespace arch {

inline bool rdrand64(uint64_t& out) {
    (void)out;
    return false;
}

inline bool rdseed64(uint64_t& out) {
    (void)out;
    return false;
}

} // namespace arch
