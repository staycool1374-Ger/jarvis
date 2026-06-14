/// @file io.hpp
/// @brief x86 port I/O and CPU control instructions.

#pragma once

#include <types.hpp>

extern "C" {
    /// @brief Write a byte to an I/O port (assembly stub).
    void arch_outb(uint16_t port, uint8_t val);
    /// @brief Read a byte from an I/O port (assembly stub).
    uint8_t arch_inb(uint16_t port);
    /// @brief Write a 16-bit word to an I/O port (assembly stub).
    void arch_outw(uint16_t port, uint16_t val);
    /// @brief Read a 16-bit word from an I/O port (assembly stub).
    uint16_t arch_inw(uint16_t port);
    /// @brief Write a 32-bit dword to an I/O port (assembly stub).
    void arch_outl(uint16_t port, uint32_t val);
    /// @brief Read a 32-bit dword from an I/O port (assembly stub).
    uint32_t arch_inl(uint16_t port);
    /// @brief HLT instruction (assembly stub).
    void arch_hlt();
    /// @brief PAUSE instruction (assembly stub).
    void arch_pause();
    /// @brief Clear interrupt flag, disable interrupts (assembly stub).
    void arch_cli();
    /// @brief Set interrupt flag, enable interrupts (assembly stub).
    void arch_sti();
}

namespace arch {

/// @brief Read Control Register 0.
/// @return Current value of CR0.
inline uint64_t read_cr0(
    ) { uint64_t value; asm volatile("mov %%cr0, %0" : "=r"(value)
    ); return value; }
/// @brief Read Control Register 2 (page fault linear address).
/// @return Current value of CR2.
inline uint64_t read_cr2(
    ) { uint64_t value; asm volatile("mov %%cr2, %0" : "=r"(value)
    ); return value; }
/// @brief Read Control Register 3 (page table base).
/// @return Current value of CR3.
inline uint64_t read_cr3(
    ) { uint64_t value; asm volatile("mov %%cr3, %0" : "=r"(value)
    ); return value; }
/// @brief Read Control Register 4.
/// @return Current value of CR4.
inline uint64_t read_cr4(
    ) { uint64_t value; asm volatile("mov %%cr4, %0" : "=r"(value)
    ); return value; }
/// @brief Write Control Register 3 (switch page table).
inline void write_cr3(uint64_t value
    ) { asm volatile("mov %0, %%cr3" : : "r"(value)
    : "memory"); }

/// @brief Writes a byte to an I/O port.
inline void outb(uint16_t port, uint8_t val) { arch_outb(port, val); }

/// @brief Reads a byte from an I/O port.
inline uint8_t inb(uint16_t port) { return arch_inb(port); }

/// @brief Writes a 16-bit word to an I/O port.
inline void outw(uint16_t port, uint16_t val) { arch_outw(port, val); }

/// @brief Reads a 16-bit word from an I/O port.
inline uint16_t inw(uint16_t port) { return arch_inw(port); }

/// @brief Writes a 32-bit dword to an I/O port.
inline void outl(uint16_t port, uint32_t val) { arch_outl(port, val); }

/// @brief Reads a 32-bit dword from an I/O port.
inline uint32_t inl(uint16_t port) { return arch_inl(port); }

/// @brief Short I/O delay (write to unused port 0x80).
inline void io_wait() { outb(0x80, 0); }

/// @brief Signals QEMU to exit with a status code (isa-debug-exit device).
inline void qemu_debug_exit(uint8_t code) { outw(0x501, code); }

/// @brief Halts the CPU until the next interrupt.
inline void hlt() { arch_hlt(); }

/// @brief PAUSE instruction (yields CPU in spin-wait loops).
inline void pause() { arch_pause(); }

/// @brief Clears the interrupt flag (disables interrupts).
inline void cli() { arch_cli(); }

/// @brief Sets the interrupt flag (enables interrupts).
inline void sti() { arch_sti(); }

/// @brief Returns whether interrupts are currently enabled (RFLAGS.IF).
inline bool interrupts_enabled() {
    uint64_t rflags = 0;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    return (rflags >> 9) & 1;
}

/// @brief Reads the timestamp counter (RDTSC).
/// @return 64-bit TSC value.
inline uint64_t rdtsc() {
    uint32_t tsc_low, tsc_high;
    asm volatile("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    return (static_cast<uint64_t>(tsc_high) << 32) | tsc_low;
}

} // namespace arch
