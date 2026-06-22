#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

inline uint64_t arch_page_table_current() { return read_cr3(); }
inline void arch_page_table_activate(uint64_t pml4_phys) { write_cr3(pml4_phys); }
inline void arch_page_table_tlb_flush(uint64_t virt_addr) {
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}
inline void arch_page_table_tlb_flush_all() {
    uint64_t cr3 = read_cr3();
    write_cr3(cr3);
}

class ArchPageTable {
public:
    static inline uint64_t current() { return arch_page_table_current(); }
    static inline void activate(uint64_t pml4_phys) { arch_page_table_activate(pml4_phys); }
    static inline void tlb_flush(uint64_t virt_addr) { arch_page_table_tlb_flush(virt_addr); }
    static inline void tlb_flush_all() { arch_page_table_tlb_flush_all(); }

    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t ENTRIES = 512;
};

} // namespace arch
