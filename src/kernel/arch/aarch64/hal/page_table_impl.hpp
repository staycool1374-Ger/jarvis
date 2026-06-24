#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

inline uint64_t arch_page_table_current() { return read_ttbr1_el1(); }
inline void arch_page_table_activate(uint64_t ttbr1_phys) {
    write_ttbr1_el1(ttbr1_phys);
    dsb_sy();
    isb();
}
inline void arch_page_table_tlb_flush(uint64_t virt_addr) {
    tlbi_vae1(virt_addr);
    dsb_sy();
    isb();
}
inline void arch_page_table_tlb_flush_all() {
    tlbi_alle1();
    dsb_sy();
    isb();
}

class ArchPageTable {
public:
    static inline uint64_t current() { return arch_page_table_current(); }
    static inline void activate(uint64_t phys) { arch_page_table_activate(phys); }
    static inline void tlb_flush(uint64_t virt_addr) { arch_page_table_tlb_flush(virt_addr); }
    static inline void tlb_flush_all() { arch_page_table_tlb_flush_all(); }

    static constexpr uint64_t PAGE_SIZE = CONFIG_PAGE_SIZE;
    static constexpr uint64_t ENTRIES = 512;
};

} // namespace arch
