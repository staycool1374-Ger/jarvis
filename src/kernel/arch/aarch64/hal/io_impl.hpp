#pragma once

#include <types.hpp>

namespace arch {

inline void mmio_write8(volatile void* addr, uint8_t val) {
    asm volatile("strb %w1, [%0]" : : "r"(addr), "r"(val) : "memory");
}
inline uint8_t mmio_read8(const volatile void* addr) {
    uint8_t val;
    asm volatile("ldrb %w0, [%1]" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
inline void mmio_write32(volatile void* addr, uint32_t val) {
    asm volatile("str %w1, [%0]" : : "r"(addr), "r"(val) : "memory");
}
inline uint32_t mmio_read32(const volatile void* addr) {
    uint32_t val;
    asm volatile("ldr %w0, [%1]" : "=r"(val) : "r"(addr) : "memory");
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
inline void pause() { asm volatile("yield"); }
inline void cli() { asm volatile("msr daifset, #2" : : : "memory"); }
inline void sti() { asm volatile("msr daifclr, #2" : : : "memory"); }

inline bool interrupts_enabled() {
    uint64_t daif;
    asm volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (1 << 7)) == 0;
}

inline uint64_t rdtsc() {
    uint64_t val;
    asm volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

inline void qemu_debug_exit(uint8_t code) {
    // ARM Semihosting SYS_EXIT (0x18) with ADP_Stopped_ApplicationExit
    uint32_t params[2] = { 0x18, code };
    uint64_t addr = reinterpret_cast<uint64_t>(params);
    __asm__ volatile(
        "mov x0, #0x18\n\t"
        "mov x1, %0\n\t"
        "hlt #0xf000"
        :
        : "r"(addr)
        : "x0", "x1", "memory");
    while (1) { asm volatile("wfi"); }
}

inline uint64_t read_ttbr0_el1() { uint64_t v; asm volatile("mrs %0, ttbr0_el1" : "=r"(v)); return v; }
inline uint64_t read_ttbr1_el1() { uint64_t v; asm volatile("mrs %0, ttbr1_el1" : "=r"(v)); return v; }
inline uint64_t read_cr3() { return read_ttbr1_el1(); }
inline uint64_t read_cr0() { return 0; }
inline void write_cr0(uint64_t) {}
inline uint64_t read_cr4() { return 0; }
inline void write_ttbr0_el1(uint64_t v) { asm volatile("msr ttbr0_el1, %0" : : "r"(v) : "memory"); }
inline void write_ttbr1_el1(uint64_t v) { asm volatile("msr ttbr1_el1, %0" : : "r"(v) : "memory"); }
inline void write_cr3(uint64_t v) { write_ttbr1_el1(v); }
inline void tlbi_vae1(uint64_t vaddr) { asm volatile("tlbi vae1, %0" : : "r"(vaddr) : "memory"); }
inline void tlbi_alle1() { asm volatile("tlbi alle1" : : : "memory"); }
inline void dsb_sy() { asm volatile("dsb sy" : : : "memory"); }
inline void isb() { asm volatile("isb" : : : "memory"); }

inline void fp_enable() {
    uint64_t cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3 << 20);    // FPEN: trap disabled
    asm volatile("msr cpacr_el1, %0" : : "r"(cpacr) : "memory");
    asm volatile("isb");
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
