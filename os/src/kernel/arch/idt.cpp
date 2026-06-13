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

void IDT::handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t rip) {
    if (vector >= NUM_ENTRIES) return;
    auto handler = handlers_[vector];
    if (handler) {
        handler(vector, error_code, rip);
    }
}

} // namespace arch
