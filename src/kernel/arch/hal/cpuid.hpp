#pragma once

#include <types.hpp>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/cpuid_impl.hpp>
#else
#  error "HAL: no cpuid implementation for this architecture"
#endif
