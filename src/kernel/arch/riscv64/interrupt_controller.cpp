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

/// @file interrupt_controller.cpp
/// @brief RISC-V64 PLIC (Platform-Level Interrupt Controller) driver.

#include <kernel/arch/interrupt_controller.hpp>
#include <kernel/arch/hal/io.hpp>
#include <kernel/arch/idt.hpp>

namespace arch {

/// @brief PLIC register memory map (QEMU virt / Renode).
#define PLIC_BASE (0x0C000000ULL)
#define PLIC_PRIORITY (PLIC_BASE + 0x000000)
#define PLIC_PENDING (PLIC_BASE + 0x001000)
#define PLIC_ENABLE (PLIC_BASE + 0x002000)
#define PLIC_THRESHOLD (PLIC_BASE + 0x200000)
#define PLIC_CLAIM (PLIC_BASE + 0x200004)

/// @brief Initialize the PLIC: clear threshold and enable S-mode external
///        interrupts in the sie CSR.
void ArchInterruptController::init() {
    uint32_t *threshold = reinterpret_cast<uint32_t *>(PLIC_THRESHOLD);
    *threshold = 0;
    asm volatile("fence iorw, iorw" : : : "memory");
    // Enable S-mode external interrupts in sie
    asm volatile("csrs sie, %0" : : "r"((uint64_t)(1ULL << 9)) : "memory");
}

/// @brief Signal end-of-interrupt by writing the vector to the PLIC claim
/// register.
/// @param vector Interrupt vector to complete.
void ArchInterruptController::eoi(uint8_t vector) {
    uint32_t *claim = reinterpret_cast<uint32_t *>(PLIC_CLAIM);
    *claim = vector;
    asm volatile("fence iorw, iorw" : : : "memory");
}

/// @brief Disable (mask) a specific IRQ in the PLIC enable registers.
/// @param irq IRQ number to mask.
void ArchInterruptController::mask(uint8_t irq) {
    uint32_t *enable = reinterpret_cast<uint32_t *>(PLIC_ENABLE);
    enable[irq / 32] &= ~(1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

/// @brief Enable (unmask) a specific IRQ in the PLIC enable registers.
/// @param irq IRQ number to unmask.
void ArchInterruptController::unmask(uint8_t irq) {
    uint32_t *enable = reinterpret_cast<uint32_t *>(PLIC_ENABLE);
    enable[irq / 32] |= (1U << (irq % 32));
    asm volatile("fence iorw, iorw" : : : "memory");
}

/// @brief Snapshot the PLIC threshold state for test isolation.
/// @return IrqState containing the current PLIC threshold value.
IrqState ArchInterruptController::snapshot() {
    IrqState s = {};
    uint32_t *threshold = reinterpret_cast<uint32_t *>(PLIC_THRESHOLD);
    s.plic_threshold = *threshold;
    return s;
}

/// @brief Restore the PLIC threshold state from a previous snapshot.
/// @param state Previously captured IrqState.
void ArchInterruptController::restore(const IrqState &state) {
    uint32_t *threshold = reinterpret_cast<uint32_t *>(PLIC_THRESHOLD);
    *threshold = state.plic_threshold;
    asm volatile("fence iorw, iorw" : : : "memory");
}

/// @brief C-level trap handler for PLIC interrupts and timer interrupts.
/// @param scause Supervisory trap cause register value.
/// @param sepc   Supervisory exception program counter.
/// @param regs   Pointer to saved register state (unused).
extern "C" void handle_plic_trap(uint64_t scause, uint64_t sepc,
                                 uint64_t *regs) {
    (void)regs;
    if (scause & (1ULL << 63)) {
        uint64_t code = scause & ~(1ULL << 63);
        if (code == 5) {
            IDT::handle_interrupt(static_cast<uint64_t>(InterruptVector::TIMER),
                                  0, sepc);
        } else if (code == 9) {
            uint32_t *claim = reinterpret_cast<uint32_t *>(PLIC_CLAIM);
            uint32_t intid = *claim;
            if (intid != 0) {
                IDT::handle_interrupt(intid, 0, sepc);
                *claim = intid;
            }
        }
    }
}

/// @brief Dump exception info via SBI ecall, then panic.
/// @param sepc   Exception program counter.
/// @param scause Exception cause register value.
/// @param stval  Exception trap value register.
/// @warning Never returns — calls panic().
extern "C" void handle_kernel_exception(uint64_t sepc, uint64_t scause,
                                        uint64_t stval) {
    // Output exception info via direct SBI ecall
    const char msg[] = {'[', 'E', 'X', 'C', ']', ' ', 's',
                        'c', 'a', 'u', 's', 'e', '=', 0};
    for (const char *p = msg; *p; ++p) {
        uint64_t ch = (unsigned char)*p;
        asm volatile("mv a0, %0; li a7, 1; ecall"
                     :
                     : "r"(ch)
                     : "a0", "a7", "memory");
    }
    // Print scause as hex digits
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
    // Print " sepc="
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
    // Print " stval="
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
    // newline
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
