/// @file rand.hpp
/// @brief Hardware random number generation instructions (RDRAND, RDSEED).

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Read a 64-bit random value using the RDRAND instruction.
/// @param[out] value Filled with a random value on success.
/// @return true if a random value was obtained (carry flag set).
inline bool rdrand64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdrand %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

/// @brief Read a 64-bit random value using the RDSEED instruction.
/// @param[out] value Filled with a random value on success.
/// @return true if a random value was obtained (carry flag set).
inline bool rdseed64(uint64_t& value) {
    unsigned char ok;
    asm volatile("rdseed %0; setc %1"
        : "=r"(value), "=qm"(ok));
    return ok != 0;
}

} // namespace arch
