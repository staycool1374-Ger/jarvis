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

/// @file constants.hpp
/// @brief Central hardware and memory-layout constants (no magic numbers).

#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

// ---------------------------------------------------------------------------
// Fundamental type limits
// ---------------------------------------------------------------------------
inline constexpr uint64_t UINT64_MAX = ~0ULL;

// ---------------------------------------------------------------------------
// Architecture: memory map, page tables, selectors, RFLAGS, I/O ports
// ---------------------------------------------------------------------------
namespace arch {

/// @brief Higher-half offset: convert physical → kernel-visible virtual.
inline constexpr uint64_t HHDM_OFFSET = CONFIG_HHDM_OFFSET;

/// @brief PML4 layout constants.
inline constexpr uint64_t PML4_USER_COUNT    = CONFIG_PML4_USER_COUNT;   // entries 0-255
inline constexpr uint64_t PML4_KERNEL_START  = CONFIG_PML4_USER_COUNT;   // entry 256
inline constexpr uint64_t PML4_ENTRIES       = 512;

inline constexpr uint64_t PAGE_SIZE   = CONFIG_PAGE_SIZE;
inline constexpr uint64_t PAGE_SIZE_2M = 0x200000;

/// @brief x86-64 segment selectors.
enum Segment : uint64_t {
    SEG_NULL        = 0x00,
    SEG_KERNEL_CODE = 0x08,
    SEG_KERNEL_DATA = 0x10,
    SEG_USER_CODE   = 0x1B,
    SEG_USER_DATA   = 0x23,
};

/// @brief RFLAGS register constants.
enum RFlags : uint64_t {
    RFLAGS_IF       = 0x200,   ///< Interrupt Flag
    RFLAGS_DEFAULT  = 0x202,   ///< IF + reserved bit 1 (task start value)
};

/// @brief COM1 serial port registers.
inline constexpr uint16_t COM1       = 0x3F8;
inline constexpr uint16_t COM1_LSR   = 0x3FD;   ///< Line Status Register

/// @brief 8259A PIC ports.
inline constexpr uint16_t PIC1_CMD   = 0x20;
inline constexpr uint16_t PIC1_DATA  = 0x21;
inline constexpr uint16_t PIC2_CMD   = 0xA0;
inline constexpr uint16_t PIC2_DATA  = 0xA1;

/// @brief QEMU-specific ACPI / shutdown ports.
inline constexpr uint16_t QEMU_ACPI_PORT      = 0x604;
inline constexpr uint16_t QEMU_SHUTDOWN_PORT  = 0xB004;

/// @brief ATA PIO registers — primary bus.
inline constexpr uint16_t ATA_DATA       = 0x1F0;
inline constexpr uint16_t ATA_ERR        = 0x1F1;
inline constexpr uint16_t ATA_SECTOR_CNT = 0x1F2;
inline constexpr uint16_t ATA_LBA_LO     = 0x1F3;
inline constexpr uint16_t ATA_LBA_MID    = 0x1F4;
inline constexpr uint16_t ATA_LBA_HI     = 0x1F5;
inline constexpr uint16_t ATA_DRIVE      = 0x1F6;
inline constexpr uint16_t ATA_CMD        = 0x1F7;
inline constexpr uint16_t ATA_ALT_STAT   = 0x3F6;

/// @brief ATA status register bits.
inline constexpr uint8_t ATA_SR_BSY  = 0x80;
inline constexpr uint8_t ATA_SR_DRDY = 0x40;
inline constexpr uint8_t ATA_SR_DRQ  = 0x08;
inline constexpr uint8_t ATA_SR_ERR  = 0x01;

/// @brief ATA commands.
inline constexpr uint8_t ATA_CMD_READ_SECTORS  = 0x20;
inline constexpr uint8_t ATA_CMD_WRITE_SECTORS = 0x30;
inline constexpr uint8_t ATA_CMD_IDENTIFY      = 0xEC;

/// @brief ATA master/slave drive select bits.
inline constexpr uint8_t ATA_DRIVE_MASTER = 0xE0;
inline constexpr uint8_t ATA_DRIVE_SLAVE  = 0xF0;

} // namespace arch

// ---------------------------------------------------------------------------
// Userspace memory layout (virtual addresses used by ELF loader and fork)
// ---------------------------------------------------------------------------
namespace mem {

inline constexpr uint64_t STACK_VADDR  = 0x70000000;
inline constexpr uint64_t HEAP_VADDR   = 0x60000000;
inline constexpr size_t   STACK_SIZE   = 64_KiB;
inline constexpr size_t   HEAP_SIZE    = 1_MiB;

} // namespace mem

// ---------------------------------------------------------------------------
// Common error codes for POSIX-style kernel functions
// ---------------------------------------------------------------------------
enum Error : int64_t {
    ERR_OK      = 0,
    ERR_FAILURE = -1,
};
