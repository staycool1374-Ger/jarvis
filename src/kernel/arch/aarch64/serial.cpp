#include <kernel/arch/serial.hpp>
#include <kernel/arch/hal/io.hpp>
#include <constants.hpp>

namespace arch {

#include <kernel/memory/address.hpp>
#define UART_BASE ((volatile uint32_t*)(arch::HHDM_OFFSET + 0x9000000ULL))

#define UART_DR     0x000
#define UART_FR     0x018
#define UART_IBRD   0x024
#define UART_FBRD   0x028
#define UART_LCR_H  0x02C
#define UART_CR     0x030
#define UART_IMSC   0x038
#define UART_ICR    0x044

void Serial::init() {
    mmio_write32(UART_BASE + UART_CR / 4, 0);
    for (int i = 0; i < 100; ++i) asm volatile("nop");
    mmio_write32(UART_BASE + UART_ICR / 4, 0x7FF);
    mmio_write32(UART_BASE + UART_IBRD / 4, 26);
    mmio_write32(UART_BASE + UART_FBRD / 4, 3);
    mmio_write32(UART_BASE + UART_LCR_H / 4, 0x70);
    mmio_write32(UART_BASE + UART_IMSC / 4, 0);
    mmio_write32(UART_BASE + UART_CR / 4, 0x301);
}

void Serial::putchar(char c) {
    if (c == '\n') {
        while (mmio_read32(UART_BASE + UART_FR / 4) & (1 << 5));
        mmio_write32(UART_BASE + UART_DR / 4, '\r');
    }
    while (mmio_read32(UART_BASE + UART_FR / 4) & (1 << 5));
    mmio_write32(UART_BASE + UART_DR / 4, c);
}

char Serial::getchar() {
    while (mmio_read32(UART_BASE + UART_FR / 4) & (1 << 4));
    return mmio_read32(UART_BASE + UART_DR / 4) & 0xFF;
}

void Serial::puts(const char* s) {
    while (*s) putchar(*s++);
}

} // namespace arch
