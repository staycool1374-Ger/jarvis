#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/arch/riscv64/hal/plic.hpp>

namespace arch {

void ArchInterruptController::init() {
    auto *threshold = reinterpret_cast<volatile uint32_t *>(PLIC_THRESHOLD);
    *threshold = 0;
    asm volatile("fence iorw, iorw" : : : "memory");
    asm volatile("csrs sie, %0" : : "r"((uint64_t)(1ULL << 9)) : "memory");
}

void ArchInterruptController::eoi(uint8_t vector) {
    plic_complete(vector);
}

void ArchInterruptController::mask(uint8_t irq) {
    auto *enable = reinterpret_cast<volatile uint32_t *>(PLIC_ENABLE);
    enable[irq / 32] &= ~(1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

void ArchInterruptController::unmask(uint8_t irq) {
    auto *enable = reinterpret_cast<volatile uint32_t *>(PLIC_ENABLE);
    enable[irq / 32] |= (1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

IrqState ArchInterruptController::snapshot() {
    IrqState s{};
    auto *threshold = reinterpret_cast<volatile uint32_t *>(PLIC_THRESHOLD);
    s.plic_threshold = *threshold;
    return s;
}

void ArchInterruptController::restore(const IrqState &state) {
    auto *threshold = reinterpret_cast<volatile uint32_t *>(PLIC_THRESHOLD);
    *threshold = state.plic_threshold;
    asm volatile("fence iorw, iorw" : : : "memory");
}

extern "C" void handle_plic_trap(uint64_t scause, uint64_t sepc,
                                 uint64_t *regs) {
    (void)regs;
    if (scause & (1ULL << 63)) {
        uint64_t code = scause & ~(1ULL << 63);
        if (code == 5) {
            IDT::handle_interrupt(
                static_cast<uint64_t>(InterruptVector::TIMER), 0, sepc);
        } else if (code == 9) {
            uint32_t intid = plic_claim();
            if (intid != 0) {
                IDT::handle_interrupt(intid, 0, sepc);
                plic_complete(intid);
            }
        }
    }
}

extern "C" void handle_kernel_exception(uint64_t sepc, uint64_t scause,
                                        uint64_t stval) {
    const char msg[] = {'[', 'E', 'X', 'C', ']', ' ', 's',
                        'c', 'a', 'u', 's', 'e', '=', 0};
    for (const char *p = msg; *p; ++p) {
        uint64_t ch = (unsigned char)*p;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    uint64_t v = scause;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (v >> i) & 0xF;
        char c = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        uint64_t ch = (unsigned char)c;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    const char msg2[] = {' ', 's', 'e', 'p', 'c', '=', 0};
    for (const char *p = msg2; *p; ++p) {
        uint64_t ch = (unsigned char)*p;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    v = sepc;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (v >> i) & 0xF;
        char c = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        uint64_t ch = (unsigned char)c;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    const char msg3[] = {' ', 's', 't', 'v', 'a', 'l', '=', 0};
    for (const char *p = msg3; *p; ++p) {
        uint64_t ch = (unsigned char)*p;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    v = stval;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (v >> i) & 0xF;
        char c = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        uint64_t ch = (unsigned char)c;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    {
        uint64_t ch = (unsigned char)'\n';
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    panic("riscv64: unhandled exception");
}

} // namespace arch
