#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/rand_impl.hpp>
#elif defined(CONFIG_ARCH_AARCH64)
#  include <kernel/arch/aarch64/hal/rand_impl.hpp>
#elif defined(CONFIG_ARCH_RISCV64)
#  include <kernel/arch/riscv64/hal/rand_impl.hpp>
#else
#  error "HAL: no rand implementation for this architecture"
#endif
