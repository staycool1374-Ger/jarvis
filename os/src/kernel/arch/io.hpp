/// @file io.hpp
/// @brief x86 port I/O and CPU control instructions (inline asm wrappers).

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Writes a byte to an I/O port.
/// @param port The I/O port address.
/// @param val  The byte value to write.
inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/// @brief Reads a byte from an I/O port.
/// @param port The I/O port address.
/// @return The byte read from the port.
inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/// @brief Writes a 16-bit word to an I/O port.
/// @param port The I/O port address.
/// @param val  The word value to write.
inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/// @brief Reads a 16-bit word from an I/O port.
/// @param port The I/O port address.
/// @return The word read from the port.
inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/// @brief Writes a 32-bit dword to an I/O port.
/// @param port The I/O port address.
/// @param val  The dword value to write.
inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/// @brief Reads a 32-bit dword from an I/O port.
/// @param port The I/O port address.
/// @return The dword read from the port.
inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/// @brief Short I/O delay (write to unused port 0x80).
inline void io_wait() {
    outb(0x80, 0);
}

/// @brief Halts the CPU until the next interrupt.
inline void hlt() {
    asm volatile("hlt");
}

/// @brief Clears the interrupt flag (disables interrupts).
inline void cli() {
    asm volatile("cli");
}

/// @brief Sets the interrupt flag (enables interrupts).
inline void sti() {
    asm volatile("sti");
}

} // namespace arch
