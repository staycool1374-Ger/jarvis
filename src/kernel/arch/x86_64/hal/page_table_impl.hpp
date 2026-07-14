#pragma once

/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file page_table_impl.hpp
/// @brief x86_64 page-table management — CR3 activation and TLB invalidation
/// wrappers.

#pragma once

#include <types.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

/// @brief Return the physical address of the currently active PML4 page table.
/// @return CR3 value (physical address of the top-level page table).
inline uint64_t arch_page_table_current() {
    return read_cr3();
}
/// @brief Activate a page table by writing its physical address into CR3.
/// @param pml4_phys Physical address of the PML4 table to activate.
inline void arch_page_table_activate(uint64_t pml4_phys) {
    write_cr3(pml4_phys);
}
/// @brief Flush the TLB entry for a single virtual address (INVLPG
/// instruction).
/// @param virt_addr Virtual address whose TLB entry should be invalidated.
inline void arch_page_table_tlb_flush(uint64_t virt_addr) {
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}
/// @brief Flush the entire TLB by reloading CR3.
inline void arch_page_table_tlb_flush_all() {
    uint64_t cr3 = read_cr3();
    write_cr3(cr3);
}

/// @brief Static wrapper class for page-table operations.
/// Provides a uniform interface for use in generic VMM code.
class ArchPageTable {
  public:
    static inline uint64_t current() {
        return arch_page_table_current();
    }
    static inline void activate(uint64_t pml4_phys) {
        arch_page_table_activate(pml4_phys);
    }
    static inline void tlb_flush(uint64_t virt_addr) {
        arch_page_table_tlb_flush(virt_addr);
    }
    static inline void tlb_flush_all() {
        arch_page_table_tlb_flush_all();
    }

    /// @brief Size of a single page (from kernel config).
    static constexpr uint64_t PAGE_SIZE = CONFIG_PAGE_SIZE;
    /// @brief Number of entries per page-table level.
    static constexpr uint64_t ENTRIES = 512;
};

} // namespace arch
