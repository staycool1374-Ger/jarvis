#pragma once

#include <types.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

namespace arch {

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

} // namespace arch
