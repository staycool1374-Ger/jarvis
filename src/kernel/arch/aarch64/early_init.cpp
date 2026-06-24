#include <types.hpp>

#define UART_DR ((volatile uint32_t*)0x9000000ULL)
static void early_putc(char c) { *UART_DR = c; }

__attribute__((noinline))
static void mmu_write_tables(void* phys_base) {
    volatile uint64_t* l0 = (volatile uint64_t*)phys_base;
    volatile uint64_t* l1 = (volatile uint64_t*)((uint64_t)phys_base + 0x1000);
    l0[0] = 0; l0[256] = 0;
    for (int i = 0; i < 512; ++i) l1[i] = 0;
    uint64_t l1_phys = (uint64_t)phys_base + 0x1000;
    l0[0]   = l1_phys | 0x3;
    l0[256] = l1_phys | 0x3;
    uint64_t attr = 0x1 | (1 << 5) | (3 << 8);
    l1[0] = 0x00000000ULL | attr;
    l1[1] = 0x40000000ULL | attr;
    l1[2] = 0x80000000ULL | attr;
    l1[3] = 0xC0000000ULL | attr;
}

extern "C" void arch_aarch64_early_init() {
    early_putc('A');
    extern uint64_t _boot_pgtables_start[];
    uint64_t phys_base = reinterpret_cast<uint64_t>(_boot_pgtables_start);
    early_putc('B');
    mmu_write_tables(reinterpret_cast<void*>(phys_base));
    early_putc('C');
    uint64_t mair = 0xFF;
    asm volatile("msr mair_el1, %0" : : "r"(mair));
    early_putc('D');
    uint64_t tcr = 0x0000800057903510ULL;
    asm volatile("msr tcr_el1, %0" : : "r"(tcr));
    early_putc('E');

    // Print phys_base low nibbles for verification
    *UART_DR = '0' + ((phys_base >> 8) & 0xF);
    *UART_DR = '0' + ((phys_base >> 4) & 0xF);
    *UART_DR = '0' + (phys_base & 0xF);

    asm volatile("msr ttbr0_el1, %0" : : "r"(phys_base));
    asm volatile("msr ttbr1_el1, %0" : : "r"(phys_base + 0x1000));
    asm volatile("dsb sy; isb" : : : "memory");
    early_putc('F');
    // DON'T enable MMU — return to boot.S
}
