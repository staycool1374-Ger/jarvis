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

/// @file dma.cpp
/// @brief DMA driver implementation.

#include <kernel/driver/dma.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/io.hpp>
#include <string.hpp>
#include <logger.hpp>

using namespace kernel;
using namespace arch;

namespace kernel::dma {

// --- DMA Buffer ---

DmaBuffer alloc_buffer(size_t size) {
    DmaBuffer buf = {};
    size_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys = PMM::alloc_contiguous(page_count);
    if (!phys) {
        Logger::error("dma: alloc_buffer(%zu) failed", size);
        return buf;
    }
    buf.phys_addr = phys;
    buf.virt_addr = HHDM_OFFSET + phys;
    buf.size      = page_count * PAGE_SIZE;
    buf.owned     = true;

    // Ensure the pages are identity-mapped for device access
    for (size_t i = 0; i < page_count; ++i) {
        VMM::map_page(phys + i * PAGE_SIZE, phys + i * PAGE_SIZE, false);
    }

    memset(reinterpret_cast<void*>(buf.virt_addr), 0, buf.size);
    return buf;
}

void free_buffer(DmaBuffer& buf) {
    if (!buf.owned || !buf.phys_addr) return;
    size_t page_count = buf.size / PAGE_SIZE;
    for (size_t i = 0; i < page_count; ++i) {
        VMM::unmap_page(buf.phys_addr + i * PAGE_SIZE);
        PMM::free_page(buf.phys_addr + i * PAGE_SIZE);
    }
    buf.phys_addr = 0;
    buf.virt_addr = 0;
    buf.size      = 0;
    buf.owned     = false;
}

// --- Scatter-Gather ---

bool sg_from_buffer(SgList& sg, const DmaBuffer& buf,
                    size_t offset, size_t length) {
    if (offset >= buf.size) return false;
    if (length == 0 || offset + length > buf.size) {
        length = buf.size - offset;
    }
    sg.count = 1;
    sg.total_length = length;
    sg.entries[0].phys_addr = buf.phys_addr + offset;
    sg.entries[0].virt_addr = buf.virt_addr + offset;
    sg.entries[0].length    = length;
    return true;
}

bool sg_from_virt(SgList& sg, uint64_t virt_addr, size_t length) {
    sg.count = 0;
    sg.total_length = 0;
    uint64_t current = virt_addr;
    uint64_t end = virt_addr + length;
    while (current < end && sg.count < DMA_MAX_SG_ENTRIES) {
        uint64_t page_start = current & ~0xFFFULL;
        uint64_t page_end   = page_start + PAGE_SIZE;
        uint64_t chunk_end  = end < page_end ? end : page_end;

        uint64_t phys = VMM::virt_to_phys(page_start);
        if (!phys) {
            Logger::error("dma: sg_from_virt: unmapped virt %x", page_start);
            return false;
        }
        size_t offset_in_page = static_cast<size_t>(current - page_start);
        auto& e = sg.entries[sg.count];
        e.phys_addr = phys + offset_in_page;
        e.virt_addr = current;
        e.length    = chunk_end - current;
        sg.total_length += e.length;
        ++sg.count;
        current = chunk_end;
    }
    return sg.count > 0;
}

void sg_reset(SgList& sg) {
    sg.count = 0;
    sg.total_length = 0;
}

// --- PRD Table ---

size_t prd_from_sg(PrdTable& prd, const SgList& sg, bool eot) {
    prd.count = 0;
    for (size_t i = 0; i < sg.count && prd.count < DMA_MAX_PRD_ENTRIES; ++i) {
        auto& entry = prd.entries[prd.count];
        entry.phys_addr  = static_cast<uint32_t>(sg.entries[i].phys_addr & 0xFFFFFFFF);
        entry.byte_count = static_cast<uint16_t>((sg.entries[i].length - 1) & 0xFFFF);
        entry.flags      = 0;
        ++prd.count;
    }
    if (prd.count > 0 && eot) {
        prd.entries[prd.count - 1].flags |= 0x8000; // EOT bit
    }
    return prd.count;
}

void prd_reset(PrdTable& prd) {
    prd.count = 0;
}

// --- PCI Bus Master ---

void pci_set_bus_master(arch::PciBdf bdf, bool enable) {
    uint32_t addr = arch::pci_make_addr(bdf, arch::PCI_COMMAND);
    uint16_t cmd = arch::pci_config_readw(addr);
    if (enable) {
        cmd |= arch::PCI_CMD_BUS_MASTER;
    } else {
        cmd &= static_cast<uint16_t>(~arch::PCI_CMD_BUS_MASTER);
    }
    arch::pci_config_writel(addr, cmd);
    Logger::info("dma: bus master %s on %d:%d.%d",
        enable ? "enabled" : "disabled",
        bdf.bus, bdf.device, bdf.function);
}

} // namespace kernel::dma
