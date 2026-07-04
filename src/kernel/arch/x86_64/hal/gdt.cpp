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

#include <kernel/arch/gdt.hpp>
#include <types.hpp>

namespace arch {

// Dedicated stack for double-fault handler (IST1)
// 4 KiB = one page, allocated in .bss (zero-initialized)
static uint8_t df_stack[4096] __attribute__((aligned(16)));

GDTEntry GDT::entries_[NUM_ENTRIES] = {};
TSS GDT::tss_ = {};
GDTDescriptor GDT::desc_ = {};

static GDTEntry make_entry(uint32_t base, uint32_t limit, uint8_t access,
    uint8_t gran) {
    GDTEntry e = {};
    e.limit_low    = limit & 0xFFFF;
    e.base_low     = base & 0xFFFF;
    e.base_mid     = (base >> 16) & 0xFF;
    e.access       = access;
    e.granularity  = gran | ((limit >> 16) & 0x0F);
    e.base_high    = (base >> 24) & 0xFF;
    return e;
}

void GDT::init() {
    entries_[0] = {};

    entries_[GDT_CODE / 8] = make_entry(0, 0, 0x9A, 0x20);
    entries_[GDT_DATA / 8] = make_entry(0, 0, 0x92, 0x00);

    entries_[GDT_USER_CODE / 8] = make_entry(0, 0, 0xFA, 0x20);
    entries_[GDT_USER_DATA / 8] = make_entry(0, 0, 0xF2, 0x00);

    uint64_t tss_base = reinterpret_cast<uint64_t>(&tss_);
    uint32_t tss_limit = sizeof(TSS) - 1;

    GDTEntry tss_low = {};
    tss_low.limit_low = tss_limit & 0xFFFF;
    tss_low.base_low  = tss_base & 0xFFFF;
    tss_low.base_mid  = (tss_base >> 16) & 0xFF;
    tss_low.access    = 0x89;
    tss_low.granularity = (tss_limit >> 16) & 0x0F;
    tss_low.base_high = (tss_base >> 24) & 0xFF;
    entries_[GDT_TSS / 8] = tss_low;

    uint64_t* tss_high = reinterpret_cast<uint64_t*>(&entries_[GDT_TSS / 8 + 1]
        );
    *tss_high = tss_base >> 32;

    desc_.limit = sizeof(entries_) - 1;
    desc_.base  = reinterpret_cast<uint64_t>(entries_);

    // Set up IST1 for double-fault handler (vector 8)
    tss_.ist1 = reinterpret_cast<uint64_t>(df_stack + sizeof(df_stack));
}

void GDT::load() {
    asm volatile("lgdt %0" : : "m"(desc_));
    asm volatile("mov %0, %%ds\n"
                 "mov %0, %%es\n"
                 "mov %0, %%fs\n"
                 "mov %0, %%gs\n"
                 "mov %0, %%ss\n" : : "r"((uint16_t)GDT_DATA));
    uint16_t tss_sel = GDT_TSS;
    asm volatile("ltr %0" : : "r"(tss_sel));
}

void GDT::set_tss_rsp0(uint64_t rsp) {
    tss_.rsp0 = rsp;
}

} // namespace arch
