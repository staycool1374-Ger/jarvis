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

/// @file dma.hpp
/// @brief DMA driver — buffer management, scatter-gather, PRD table construction.

#pragma once

#include <types.hpp>
#include <constants.hpp>
#include <kernel/arch/pci.hpp>

namespace kernel::dma {

/// Maximum entries in a scatter-gather list or PRD table.
constexpr size_t DMA_MAX_SG_ENTRIES = 256;
constexpr size_t DMA_MAX_PRD_ENTRIES = 256;

/// DMA direction
enum class Direction : uint8_t {
    READ  = 0,  // device reads from memory (memory → device)
    WRITE = 1,  // device writes to memory (device → memory)
};

/// A single scatter-gather entry
struct SgEntry {
    uint64_t phys_addr;
    uint64_t length;
    uint64_t virt_addr;
};

/// Scatter-gather list
struct SgList {
    SgEntry  entries[DMA_MAX_SG_ENTRIES];
    size_t   count;
    uint64_t total_length;
};

/// Physical Region Descriptor (ATA bus master format)
struct PrdEntry {
    uint32_t phys_addr;
    uint16_t byte_count;    // actual byte count - 1 (0 means 65536)
    uint16_t flags;         // bit 15 = EOT
} __attribute__((packed));

/// PRD table — fixed-size array of PrdEntry
struct PrdTable {
    PrdEntry entries[DMA_MAX_PRD_ENTRIES];
    size_t   count;
};

// --- DMA Buffer ---

/// A physically contiguous DMA buffer.
struct DmaBuffer {
    uint64_t phys_addr;
    uint64_t virt_addr;
    size_t   size;
    bool     owned;  // true if we allocated it and should free on destroy
};

/// Allocate a physically contiguous DMA buffer.
/// @param size  Size in bytes (will be rounded up to PAGE_SIZE).
/// @return DmaBuffer (phys_addr=0 on failure).
DmaBuffer alloc_buffer(size_t size);

/// Free a previously allocated DMA buffer.
void free_buffer(DmaBuffer& buf);

// --- Scatter-Gather ---

/// Build a scatter-gather list from a DMA buffer and optional offset/length.
/// For a single physically contiguous buffer, the SG list has one entry.
bool sg_from_buffer(SgList& sg, const DmaBuffer& buf,
                    size_t offset = 0, size_t length = 0);

/// Build a scatter-gather list from a virtual address range (which may cross
/// page boundaries). Translates virtual addresses to physical via VMM.
bool sg_from_virt(SgList& sg, uint64_t virt_addr, size_t length);

/// Reset a scatter-gather list.
void sg_reset(SgList& sg);

// --- PRD Table ---

/// Build a PRD table from a scatter-gather list.
/// ATA bus master PRD format: 32-bit phys addr, 16-bit byte count-1, 16-bit flags.
/// The table is suitable for programming a PCI bus master controller.
/// @param prd   Output PRD table (must have backing memory accessible by device).
/// @param sg    Input scatter-gather list.
/// @param eot   If true, sets the EOT flag on the last entry.
/// @return Number of PRD entries written.
size_t prd_from_sg(PrdTable& prd, const SgList& sg, bool eot = true);

/// Reset a PRD table.
void prd_reset(PrdTable& prd);

// --- PCI Bus Master ---

/// Enable/disable PCI bus mastering for a device.
/// @param bdf     BDF address.
/// @param enable  True to enable bus mastering, false to disable.
void pci_set_bus_master(arch::PciBdf bdf, bool enable);

} // namespace kernel::dma
