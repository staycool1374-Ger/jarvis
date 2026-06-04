/// @file multiboot2.hpp
/// @brief Multiboot2 structures for boot information parsing.

#pragma once

#include <types.hpp>

/// @brief Generic Multiboot2 tag header.
struct Multiboot2Tag {
    uint32_t type;
    uint32_t size;
};

/// @brief Multiboot2 information structure header.
struct Multiboot2Info {
    uint32_t total_size;
    uint32_t reserved;
};

/// @brief Framebuffer information tag from Multiboot2.
struct FramebufferTag {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type_specific;
    uint8_t  reserved[2];
};

/// @brief Entry in the memory map from Multiboot2.
struct MemoryMapEntry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

/// @brief Memory map tag containing variable-length entries.
struct MemoryMapTag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    MemoryMapEntry entries[];
};
extern "C" {
    /// @brief Multiboot2 magic value (0x36D76289 if booted by Multiboot2).
    extern uint32_t multiboot_magic;
    /// @brief Physical pointer to the Multiboot2 info structure.
    extern uint32_t multiboot_info_ptr;
}

/// @brief Finds a Multiboot2 tag by type.
/// @param type The tag type to search for.
/// @return Physical address of the tag, or 0 if not found.
inline uint64_t mb2_find_tag(uint32_t type) {
    if (multiboot_magic != 0x36D76289) return 0;

    auto* info = reinterpret_cast<Multiboot2Info*>(
        static_cast<uint64_t>(multiboot_info_ptr));

    uint64_t addr = multiboot_info_ptr + 8;
    while (addr < multiboot_info_ptr + info->total_size) {
        auto* tag = reinterpret_cast<Multiboot2Tag*>(addr);
        if (tag->type == 0) break;
        if (tag->type == type) return addr;
        addr += (tag->size + 7) & ~7;
    }
    return 0;
}
