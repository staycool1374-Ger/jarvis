#pragma once

#include <types.hpp>

#define BOOTINFO_MAX_MEM_REGIONS 8

struct BootInfo {
    struct MemRegion {
        uint64_t base;
        uint64_t size;
        uint32_t type;  // 1 = usable, 2 = reserved, 3 = acpi, 4 = mmio
    };

    MemRegion mem_regions[BOOTINFO_MAX_MEM_REGIONS];
    int num_mem_regions;
    uint64_t total_mem_size;   // total usable RAM
    uint64_t dtb_ptr;          // device tree pointer (0 if not available)
    uint64_t multiboot_magic;  // multiboot magic (0 if not available)
    uint64_t multiboot_info;   // multiboot info pointer (0 if not available)
    char cmdline[256];         // boot command line
    int hart_id;               // hart ID (riscv64 only)

    BootInfo() : num_mem_regions(0), total_mem_size(0), dtb_ptr(0),
                 multiboot_magic(0), multiboot_info(0), hart_id(0) {
        cmdline[0] = '\0';
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void add_region(uint64_t base, uint64_t size, uint32_t type) {
        if (num_mem_regions < BOOTINFO_MAX_MEM_REGIONS) {
            mem_regions[num_mem_regions].base = base;
            mem_regions[num_mem_regions].size = size;
            mem_regions[num_mem_regions].type = type;
            num_mem_regions++;
            if (type == 1) total_mem_size += size;
        }
    }
};
