#include <kernel/arch/serial.hpp>

namespace arch {

void Serial::init() {
}

void Serial::putchar(char c) {
    if (c == '\n') {
        asm volatile("li a0, 13; li a7, 1; ecall" : : : "a0", "a7", "memory");
    }
    uint64_t ch = (unsigned char)c;
    asm volatile("mv a0, %0; li a7, 1; ecall" : : "r"(ch) : "a0", "a7", "memory");
}

void Serial::puts(const char* s) {
    while (*s) putchar(*s++);
}

} // namespace arch
