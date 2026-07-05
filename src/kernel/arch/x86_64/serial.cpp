#include <kernel/arch/serial.hpp>
#include <kernel/arch/hal/io.hpp>
#include <constants.hpp>

namespace arch {

void Serial::init() {
    outb(arch::COM1 + 1, 0x00);
    outb(arch::COM1 + 3, 0x80);
    outb(arch::COM1 + 0, 0x01);
    outb(arch::COM1 + 1, 0x00);
    outb(arch::COM1 + 3, 0x03);
    outb(arch::COM1 + 2, 0xC7);
    outb(arch::COM1 + 4, 0x0B);
    outb(arch::COM1 + 4, 0x0F);
}

void Serial::putchar(char c) {
    if (c == '\n') {
        while ((inb(arch::COM1 + 5) & 0x20) == 0);
        outb(arch::COM1, '\r');
    }
    while ((inb(arch::COM1 + 5) & 0x20) == 0);
    outb(arch::COM1, c);
}

char Serial::getchar() {
    while ((inb(arch::COM1 + 5) & 0x01) == 0);
    return static_cast<char>(inb(arch::COM1));
}

void Serial::puts(const char* s) {
    while (*s) putchar(*s++);
}

} // namespace arch
