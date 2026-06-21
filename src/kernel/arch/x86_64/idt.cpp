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

#include <kernel/arch/idt.hpp>
#include <kernel/arch/gdt.hpp>
#include <types.hpp>
#include <assert.hpp>

namespace arch {

extern "C" uint64_t __isr_vector[];

IDTEntry IDT::entries_[NUM_ENTRIES] = {};
IDTDescriptor IDT::desc_ = {};
ISRHandler IDT::handlers_[NUM_ENTRIES] = {};

static IDTEntry make_entry(uint64_t handler, uint16_t selector,
    uint8_t type_attr, uint8_t ist) {
    IDTEntry e = {};
    e.offset_low  = handler & 0xFFFF;
    e.selector    = selector;
    e.ist         = ist & 0x7;
    e.type_attr   = type_attr;
    e.offset_mid  = (handler >> 16) & 0xFFFF;
    e.offset_high = (handler >> 32) & 0xFFFFFFFF;
    e.zero        = 0;
    return e;
}

void IDT::init() {
    for (size_t i = 0; i < NUM_ENTRIES; ++i) {
        entries_[i] = make_entry(__isr_vector[i], GDT_CODE, 0x8E, 0);
    }

    entries_[static_cast<int>(InterruptVector::SYSCALL)] =
        make_entry(__isr_vector[static_cast<int>(InterruptVector::SYSCALL)],
                   GDT_CODE, 0xEE, 0);

    desc_.limit = sizeof(entries_) - 1;
    desc_.base  = reinterpret_cast<uint64_t>(entries_);
}

void IDT::load() {
    asm volatile("lidt %0" : : "m"(desc_));
}

void IDT::register_handler(InterruptVector vec, ISRHandler handler) {
    int idx = static_cast<int>(vec);
    if (idx < 0 || static_cast<size_t>(idx) >= NUM_ENTRIES) return;
    handlers_[idx] = handler;
}

void IDT::register_handler_raw(uint8_t vec, ISRHandler handler) {
    if (static_cast<size_t>(vec) >= NUM_ENTRIES) return;
    handlers_[vec] = handler;
}

void IDT::handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t rip) {
    if (vector >= NUM_ENTRIES) return;
    auto handler = handlers_[vector];
    if (handler) {
        handler(vector, error_code, rip);
    }
}

const IDTEntry& IDT::entry(uint8_t vec) {
    return entries_[vec];
}

bool IDT::has_handler(size_t vec) {
    if (vec >= NUM_ENTRIES) return false;
    const auto& e = entries_[vec];
    uint64_t handler = static_cast<uint64_t>(e.offset_high) << 32 |
                       static_cast<uint64_t>(e.offset_mid) << 16 |
                       e.offset_low;
    return handler != 0;
}

} // namespace arch
