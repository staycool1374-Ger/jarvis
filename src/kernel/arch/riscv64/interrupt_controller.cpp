#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/idt.hpp>

namespace arch {

// PLIC memory map (QEMU virt, matching Renode .repl)
#define PLIC_BASE        (0x0C000000ULL)
#define PLIC_PRIORITY    (PLIC_BASE + 0x000000)
#define PLIC_PENDING     (PLIC_BASE + 0x001000)
#define PLIC_ENABLE      (PLIC_BASE + 0x002000)
#define PLIC_THRESHOLD   (PLIC_BASE + 0x200000)
#define PLIC_CLAIM       (PLIC_BASE + 0x200004)

void ArchInterruptController::init() {
    uint32_t* threshold = reinterpret_cast<uint32_t*>(PLIC_THRESHOLD);
    *threshold = 0;
    asm volatile("fence iorw, iorw" : : : "memory");
    // Enable S-mode external interrupts in sie
    asm volatile("csrs sie, %0" : : "r"((uint64_t)(1ULL << 9)) : "memory");
}

void ArchInterruptController::eoi(uint8_t vector) {
    uint32_t* claim = reinterpret_cast<uint32_t*>(PLIC_CLAIM);
    *claim = vector;
    asm volatile("fence iorw, iorw" : : : "memory");
}

void ArchInterruptController::mask(uint8_t irq) {
    uint32_t* enable = reinterpret_cast<uint32_t*>(PLIC_ENABLE);
    enable[irq / 32] &= ~(1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

void ArchInterruptController::unmask(uint8_t irq) {
    uint32_t* enable = reinterpret_cast<uint32_t*>(PLIC_ENABLE);
    enable[irq / 32] |= (1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

IrqState ArchInterruptController::snapshot() {
    IrqState s = {};
    uint32_t* threshold = reinterpret_cast<uint32_t*>(PLIC_THRESHOLD);
    s.plic_threshold = *threshold;
    return s;
}

void ArchInterruptController::restore(const IrqState& state) {
    uint32_t* threshold = reinterpret_cast<uint32_t*>(PLIC_THRESHOLD);
    *threshold = state.plic_threshold;
    asm volatile("fence iorw, iorw" : : : "memory");
}

extern "C" void handle_plic_trap(uint64_t scause, uint64_t sepc, uint64_t* regs) {
    (void)regs;
    if (scause & (1ULL << 63)) {
        uint64_t code = scause & ~(1ULL << 63);
        if (code == 5) {
            IDT::handle_interrupt(static_cast<uint64_t>(InterruptVector::TIMER), 0, sepc);
        } else if (code == 9) {
            uint32_t* claim = reinterpret_cast<uint32_t*>(PLIC_CLAIM);
            uint32_t intid = *claim;
            if (intid != 0) {
                IDT::handle_interrupt(intid, 0, sepc);
                *claim = intid;
            }
        }
    }
}

extern "C" void handle_kernel_exception(uint64_t sepc, uint64_t scause, uint64_t stval) {
    // TODO: log exception details
    (void)sepc;
    (void)scause;
    (void)stval;
    panic("riscv64: unhandled exception");
}

} // namespace arch
