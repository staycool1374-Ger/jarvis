#pragma once

#include <types.hpp>
#include <constants.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

struct IrqState {
    uint16_t pic1_mask;
    uint16_t pic2_mask;
};

class ArchInterruptController {
public:
    static inline void init() {
        outb(arch::PIC1_CMD, 0x11);
        outb(arch::PIC2_CMD, 0x11);
        outb(arch::PIC1_DATA, 0x20);
        outb(arch::PIC2_DATA, 0x28);
        outb(arch::PIC1_DATA, 0x04);
        outb(arch::PIC2_DATA, 0x02);
        outb(arch::PIC1_DATA, 0x01);
        outb(arch::PIC2_DATA, 0x01);
        outb(arch::PIC1_DATA, 0x00);
        outb(arch::PIC2_DATA, 0x00);
    }

    static inline void eoi(uint8_t vector) {
        outb(arch::PIC1_CMD, 0x20);
        if (vector >= 40) outb(arch::PIC2_CMD, 0x20);
    }

    static inline void mask(uint8_t irq) {
        uint16_t port = (irq < 8) ? arch::PIC1_DATA : arch::PIC2_DATA;
        uint8_t bit = 1 << (irq & 7);
        uint8_t m = inb(port) | bit;
        outb(port, m);
    }

    static inline void unmask(uint8_t irq) {
        uint16_t port = (irq < 8) ? arch::PIC1_DATA : arch::PIC2_DATA;
        uint8_t bit = ~(1 << (irq & 7));
        uint8_t m = inb(port) & bit;
        outb(port, m);
    }

    static inline IrqState snapshot() {
        IrqState s;
        s.pic1_mask = inb(arch::PIC1_DATA);
        s.pic2_mask = inb(arch::PIC2_DATA);
        return s;
    }

    static inline void restore(const IrqState& state) {
        outb(arch::PIC1_DATA, state.pic1_mask);
        outb(arch::PIC2_DATA, state.pic2_mask);
    }
};

#elif defined(CONFIG_ARCH_AARCH64)

struct IrqState {
    uint64_t gic_mask;
};

class ArchInterruptController {
public:
    static void init();
    static void eoi(uint8_t vector);
    static void mask(uint8_t irq);
    static void unmask(uint8_t irq);
    static IrqState snapshot();
    static void restore(const IrqState& state);
};

#elif defined(CONFIG_ARCH_RISCV64)

struct IrqState {
    uint32_t plic_threshold;
};

class ArchInterruptController {
public:
    static void init();
    static void eoi(uint8_t vector);
    static void mask(uint8_t irq);
    static void unmask(uint8_t irq);
    static IrqState snapshot();
    static void restore(const IrqState& state);
};

#else
#  error "HAL: no interrupt_controller implementation for this architecture"
#endif

} // namespace arch
