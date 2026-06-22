#pragma once

#include <types.hpp>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/rand_impl.hpp>
#else
#  error "HAL: no rand implementation for this architecture"
#endif
