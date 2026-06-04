#include <kernel/arch/gdt.hpp>
#include <types.hpp>

namespace arch {

GDTEntry GDT::entries_[NUM_ENTRIES] = {};
TSS GDT::tss_ = {};
GDTDescriptor GDT::desc_ = {};

static GDTEntry make_entry(uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    GDTEntry e;
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

    uint64_t* tss_high = reinterpret_cast<uint64_t*>(&entries_[GDT_TSS / 8 + 1]);
    *tss_high = tss_base >> 32;

    desc_.limit = sizeof(entries_) - 1;
    desc_.base  = reinterpret_cast<uint64_t>(entries_);
}

void GDT::load() {
    asm volatile("lgdt %0" : : "m"(desc_));
    asm volatile("mov %0, %%ds\n"
                 "mov %0, %%es\n"
                 "mov %0, %%fs\n"
                 "mov %0, %%gs\n"
                 "mov %0, %%ss\n" : : "r"((uint16_t)GDT_DATA));
}

void GDT::set_tss_rsp0(uint64_t rsp) {
    tss_.rsp0 = rsp;
    uint16_t tss_sel = GDT_TSS;
    asm volatile("ltr %0" : : "r"(tss_sel));
}

} // namespace arch
