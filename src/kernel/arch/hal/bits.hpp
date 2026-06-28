#pragma once

#include <types.hpp>

namespace hal::bits {

static inline uint64_t find_highest_bit(uint64_t mask) noexcept {
    if (mask == 0) return 0;
#if defined(CONFIG_ARCH_X86_64) || defined(CONFIG_ARCH_AARCH64) || defined(__GNUC__)
    return 63ULL - static_cast<uint64_t>(__builtin_clzll(mask));
#else
    uint64_t n = 0;
    if (mask & 0xFFFFFFFF00000000ULL) { n |= 32; mask >>= 32; }
    if (mask & 0xFFFF0000ULL)         { n |= 16; mask >>= 16; }
    if (mask & 0xFF00ULL)             { n |= 8;  mask >>= 8;  }
    if (mask & 0xF0ULL)              { n |= 4;  mask >>= 4;  }
    if (mask & 0xCULL)               { n |= 2;  mask >>= 2;  }
    if (mask & 0x2ULL)               { n |= 1; }
    return n;
#endif
}

static inline uint64_t find_lowest_bit(uint64_t mask) noexcept {
    if (mask == 0) return 0;
#if defined(CONFIG_ARCH_X86_64) || defined(CONFIG_ARCH_AARCH64) || defined(__GNUC__)
    return static_cast<uint64_t>(__builtin_ctzll(mask));
#else
    uint64_t n = 0;
    if (!(mask & 0xFFFFFFFF)) { n |= 32; mask >>= 32; }
    if (!(mask & 0xFFFF))     { n |= 16; mask >>= 16; }
    if (!(mask & 0xFF))       { n |= 8;  mask >>= 8;  }
    if (!(mask & 0xF))        { n |= 4;  mask >>= 4;  }
    if (!(mask & 0x3))        { n |= 2;  mask >>= 2;  }
    if (!(mask & 0x1))        { n |= 1; }
    return n;
#endif
}

} // namespace hal::bits
