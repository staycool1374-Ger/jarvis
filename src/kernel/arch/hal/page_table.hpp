#pragma once

#include <types.hpp>

#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/page_table_impl.hpp>
#else
#  error "HAL: no page_table implementation for this architecture"
#endif
