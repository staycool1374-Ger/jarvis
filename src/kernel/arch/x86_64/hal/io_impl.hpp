#pragma once

#include <types.hpp>

// x86_64 inline implementations for HAL I/O interface.

namespace arch {

inline uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
inline uint64_t read_cr4() { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
inline void write_cr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
inline void write_cr0(uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
inline void write_cr4(uint64_t v) { asm volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }

inline void fninit() { asm volatile("fninit"); }
inline void ldmxcsr(uint32_t mxcsr) { asm volatile("ldmxcsr %0" : : "m"(mxcsr)); }
inline void fxsave(void* buf) { asm volatile("fxsave %0" : "=m"(*static_cast<uint64_t(*)[64]>(buf)) : : "memory"); }
inline void fxrstor(const void* buf) { asm volatile("fxrstor %0" : : "m"(*static_cast<const uint64_t(*)[64]>(buf)) : "memory"); }

inline void outb(uint16_t port, uint8_t val) { arch_outb(port, val); }
inline uint8_t inb(uint16_t port) { return arch_inb(port); }
inline void outw(uint16_t port, uint16_t val) { arch_outw(port, val); }
inline uint16_t inw(uint16_t port) { return arch_inw(port); }
inline void outl(uint16_t port, uint32_t val) { arch_outl(port, val); }
inline uint32_t inl(uint16_t port) { return arch_inl(port); }
inline void io_wait() { outb(0x80, 0); }
inline void qemu_debug_exit(uint8_t code) { outb(0x501, code); }
inline void hlt() { arch_hlt(); }
inline void pause() { arch_pause(); }
inline void cli() { arch_cli(); }
inline void sti() { arch_sti(); }

inline bool interrupts_enabled() {
    uint64_t rflags = 0;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    return (rflags >> 9) & 1;
}

inline uint64_t rdtsc() {
    uint32_t tsc_low, tsc_high;
    asm volatile("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    return (static_cast<uint64_t>(tsc_high) << 32) | tsc_low;
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
