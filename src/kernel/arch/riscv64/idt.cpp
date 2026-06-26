#include <kernel/arch/idt.hpp>
#include <kernel/arch/io.hpp>
#include <types.hpp>

extern "C" void trap_vector(void);

namespace arch {

ISRHandler IDT::handlers_[NUM_ENTRIES] = {};

void IDT::init() {
    asm volatile("csrw stvec, %0" : : "r"((uint64_t)trap_vector) : "memory");
}

void IDT::load() {
    asm volatile("" : : : "memory");
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

void IDT::handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t pc) {
    if (vector >= NUM_ENTRIES) return;
    auto handler = handlers_[vector];
    if (handler) {
        handler(vector, error_code, pc);
    }
}

} // namespace arch
