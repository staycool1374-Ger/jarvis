/// @file io.hpp
/// @brief x86 port I/O and CPU control instructions.

#pragma once

#include <types.hpp>

extern "C" {
    void arch_outb(uint16_t port, uint8_t val);
    uint8_t arch_inb(uint16_t port);
    void arch_outw(uint16_t port, uint16_t val);
    uint16_t arch_inw(uint16_t port);
    void arch_outl(uint16_t port, uint32_t val);
    uint32_t arch_inl(uint16_t port);
    void arch_hlt();
    void arch_pause();
    void arch_cli();
    void arch_sti();
}

namespace arch {

inline uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
inline uint64_t read_cr4() { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
inline void write_cr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }

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

/// @brief Reads the timestamp counter (RDTSC).
/// @return 64-bit TSC value.
inline uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

} // namespace arch
