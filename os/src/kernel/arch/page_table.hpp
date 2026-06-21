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
