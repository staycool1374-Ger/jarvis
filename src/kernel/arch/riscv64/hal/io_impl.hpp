#pragma once

#include <types.hpp>

namespace arch {

inline void mmio_write8(volatile void* addr, uint8_t val) {
    asm volatile("sb %1, 0(%0)" : : "r"(addr), "r"(val) : "memory");
}
inline uint8_t mmio_read8(const volatile void* addr) {
    uint8_t val;
    asm volatile("lbu %0, 0(%1)" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
inline void mmio_write32(volatile void* addr, uint32_t val) {
    asm volatile("sw %1, 0(%0)" : : "r"(addr), "r"(val) : "memory");
}
inline uint32_t mmio_read32(const volatile void* addr) {
    uint32_t val;
    asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

inline void outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
inline uint8_t inb(uint16_t port) { (void)port; return 0; }
inline void outw(uint16_t port, uint16_t val) { (void)port; (void)val; }
inline uint16_t inw(uint16_t port) { (void)port; return 0; }
inline void outl(uint16_t port, uint32_t val) { (void)port; (void)val; }
inline uint32_t inl(uint16_t port) { (void)port; return 0; }
inline void io_wait() { asm volatile("nop"); }

inline void hlt() { asm volatile("wfi"); }
inline void pause() { asm volatile("nop"); }
inline void cli() {
    asm volatile("csrc sstatus, %0" : : "r"((uint64_t)(1 << 1)) : "memory");
}
inline void sti() {
    asm volatile("csrs sstatus, %0" : : "r"((uint64_t)(1 << 1)) : "memory");
}

inline bool interrupts_enabled() {
    uint64_t sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    return (sstatus & (1 << 1)) != 0;
}

inline uint64_t rdtsc() {
    uint64_t val;
    asm volatile("csrr %0, time" : "=r"(val));
    return val;
}

inline void qemu_debug_exit(uint8_t) {
    // SBI_SHUTDOWN via ECALL
    asm volatile(
        "li a7, 8\n\t"
        "ecall"
        : : : "a7", "memory");
    while (1) { asm volatile("wfi"); }
}

inline void sfence_vma() { asm volatile("sfence.vma" : : : "memory"); }
inline void sfence_vma_va(uint64_t vaddr) { asm volatile("sfence.vma %0" : : "r"(vaddr) : "memory"); }

inline uint64_t read_satp() { uint64_t v; asm volatile("csrr %0, satp" : "=r"(v)); return v; }
inline void write_satp(uint64_t v) { asm volatile("csrw satp, %0" : : "r"(v) : "memory"); sfence_vma(); }

inline uint64_t read_cr3() { return read_satp() & 0xFFFFFFFFFFF; } // extract PPN from satp
inline void write_cr3(uint64_t v) { write_satp((8ULL << 60) | v); } // Sv39 mode
inline void write_cr3_notlbi(uint64_t v) { write_satp((8ULL << 60) | v); }

inline uint64_t read_cr0() { return 0; }
inline void write_cr0(uint64_t) {}
inline uint64_t read_cr4() { return 0; }

inline void fp_enable() {
    // RISC-V: FS field in mstatus/sstatus, set to 0b11 (dirty)
    uint64_t sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus |= (3ULL << 13);
    asm volatile("csrw sstatus, %0" : : "r"(sstatus) : "memory");
}

class ArchIO {
public:
    static inline void outb(uint16_t port, uint8_t val) { arch::outb(port, val); }
    static inline uint8_t inb(uint16_t port) { return arch::inb(port); }
    static inline void outw(uint16_t port, uint16_t val) { arch::outw(port, val); }
    static inline uint16_t inw(uint16_t port) { return arch::inw(port); }
    static inline void outl(uint16_t port, uint32_t val) { arch::outl(port, val); }
    static inline uint32_t inl(uint16_t port) { return arch::inl(port); }
    static inline void io_wait() { arch::io_wait(); }
    static inline void hlt() { arch::hlt(); }
    static inline void pause() { arch::pause(); }
    static inline void cli() { arch::cli(); }
    static inline void sti() { arch::sti(); }
};

} // namespace arch
