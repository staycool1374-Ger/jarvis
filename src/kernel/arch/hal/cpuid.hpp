#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/cpuid_impl.hpp>
#elif defined(CONFIG_ARCH_AARCH64)
#  include <kernel/arch/aarch64/hal/cpuid_impl.hpp>
#else
#  error "HAL: no cpuid implementation for this architecture"
#endif
