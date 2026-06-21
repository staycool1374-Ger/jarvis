#pragma once

#include <types.hpp>
#include <kernel/arch/io.hpp>

namespace arch {

class ArchPageTable {
public:
    static inline uint64_t current() { return read_cr3(); }
    static inline void activate(uint64_t pml4_phys) { write_cr3(pml4_phys); }
    static inline void tlb_flush(uint64_t virt_addr) {
        asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    }
    static inline void tlb_flush_all() {
        uint64_t cr3 = read_cr3();
        write_cr3(cr3);
    }

    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t ENTRIES = 512;
};

} // namespace arch
