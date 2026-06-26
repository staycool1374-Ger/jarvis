#pragma once

#include <types.hpp>
#include <constants.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

class Serial {
public:
    static void init() {
        outb(arch::COM1 + 1, 0x00);
        outb(arch::COM1 + 3, 0x80);
        outb(arch::COM1 + 0, 0x01);
        outb(arch::COM1 + 1, 0x00);
        outb(arch::COM1 + 3, 0x03);
        outb(arch::COM1 + 2, 0xC7);
        outb(arch::COM1 + 4, 0x0B);
        outb(arch::COM1 + 4, 0x0F);
    }

    static void putchar(char c) {
        if (c == '\n') {
            while ((inb(arch::COM1 + 5) & 0x20) == 0);
            outb(arch::COM1, '\r');
        }
        while ((inb(arch::COM1 + 5) & 0x20) == 0);
        outb(arch::COM1, c);
    }

    static void puts(const char* s) {
        while (*s) putchar(*s++);
    }
};

#elif defined(CONFIG_ARCH_AARCH64)

class Serial {
public:
    static void init();
    static void putchar(char c);
    static void puts(const char* s);
};

#elif defined(CONFIG_ARCH_RISCV64)

class Serial {
public:
    static void init();
    static void putchar(char c);
    static void puts(const char* s);
};

#else
#  error "HAL: no serial implementation for this architecture"
#endif

} // namespace arch
