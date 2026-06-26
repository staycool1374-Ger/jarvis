#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/msr_impl.hpp>
#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)
// AArch64/RISC-V do not have x86-style MSRs; system registers use CSR directly.
// Provide empty stubs so code that conditionally calls wrmsr/rdmsr compiles.
namespace arch {
inline void wrmsr(uint32_t, uint64_t) {}
inline uint64_t rdmsr(uint32_t) { return 0; }
}
#else
#  error "HAL: no msr implementation for this architecture"
#endif
