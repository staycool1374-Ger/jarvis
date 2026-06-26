#pragma once

#include <types.hpp>

namespace arch {

inline void wrmsr(uint32_t addr, uint64_t val) { (void)addr; (void)val; }
inline uint64_t rdmsr(uint32_t addr) { (void)addr; return 0; }

} // namespace arch
