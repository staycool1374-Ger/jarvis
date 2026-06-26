#pragma once

#include <types.hpp>

#if defined(CONFIG_ARCH_X86_64)
// Architecture-agnostic declarations (asm stubs provided by arch linker)
extern "C" {
    void arch_outb(uint16_t port, uint8_t val);
    uint8_t arch_inb(uint16_t port);
    void arch_outw(uint16_t port, uint16_t val);
    uint16_t arch_inw(uint16_t port);
    void arch_outl(uint16_t port, uint32_t val);
    uint32_t arch_inl(uint16_t port);
    void arch_hlt();
    void arch_pause();
    void arch_cli();
    void arch_sti();
}
#endif

// Include arch-specific inline implementations
#if defined(CONFIG_ARCH_X86_64)
#  include <kernel/arch/x86_64/hal/io_impl.hpp>
#elif defined(CONFIG_ARCH_AARCH64)
#  include <kernel/arch/aarch64/hal/io_impl.hpp>
#elif defined(CONFIG_ARCH_RISCV64)
#  include <kernel/arch/riscv64/hal/io_impl.hpp>
#else
#  error "HAL: no io implementation for this architecture"
#endif
