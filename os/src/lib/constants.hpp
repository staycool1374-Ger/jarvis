/// @file constants.hpp
/// @brief Central hardware and memory-layout constants (no magic numbers).

#pragma once

#include <types.hpp>

// ---------------------------------------------------------------------------
// Architecture: memory map, page tables, selectors, RFLAGS, I/O ports
// ---------------------------------------------------------------------------
namespace arch {

/// @brief Higher-half offset: convert physical → kernel-visible virtual.
inline constexpr uint64_t HHDM_OFFSET = 0xFFFF800000000000ULL;

/// @brief PML4 layout constants.
inline constexpr uint64_t PML4_USER_COUNT    = 256;   // entries 0-255
inline constexpr uint64_t PML4_KERNEL_START  = 256;   // entry 256
inline constexpr uint64_t PML4_ENTRIES       = 512;

inline constexpr uint64_t PAGE_SIZE   = 4096;
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
