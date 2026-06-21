/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <types.hpp>
#include <constants.hpp>
#include <kernel/arch/io.hpp>

namespace arch {

struct IrqState {
    uint16_t pic1_mask;
    uint16_t pic2_mask;
};

class ArchInterruptController {
public:
    static inline void init() {
        outb(PIC1_CMD, 0x11);
        outb(PIC2_CMD, 0x11);
        outb(PIC1_DATA, 0x20);
        outb(PIC2_DATA, 0x28);
        outb(PIC1_DATA, 0x04);
        outb(PIC2_DATA, 0x02);
        outb(PIC1_DATA, 0x01);
        outb(PIC2_DATA, 0x01);
        outb(PIC1_DATA, 0x00);
        outb(PIC2_DATA, 0x00);
    }

    static inline void eoi(uint8_t vector) {
        outb(PIC1_CMD, 0x20);
        if (vector >= 40) outb(PIC2_CMD, 0x20);
    }

    static inline void mask(uint8_t irq) {
        uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
        uint8_t bit = 1 << (irq & 7);
        uint8_t mask = inb(port) | bit;
        outb(port, mask);
    }

    static inline void unmask(uint8_t irq) {
        uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
        uint8_t bit = ~(1 << (irq & 7));
        uint8_t mask = inb(port) & bit;
        outb(port, mask);
    }

    static inline IrqState snapshot() {
        IrqState s;
        s.pic1_mask = inb(PIC1_DATA);
        s.pic2_mask = inb(PIC2_DATA);
        return s;
    }

    static inline void restore(const IrqState& state) {
        outb(PIC1_DATA, state.pic1_mask);
        outb(PIC2_DATA, state.pic2_mask);
    }
};

} // namespace arch
