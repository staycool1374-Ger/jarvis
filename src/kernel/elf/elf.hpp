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

/// @file elf.hpp
/// @brief ELF64 binary loader — validate, load, exec.

#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace elf {

/// @brief ELF64 file header (64-byte packed structure).
struct ELF64Header {
    uint8_t ident[16];  ///< ELF magic + class/endian/version/OSABI/pad.
    uint16_t type;      ///< Object file type (ET_NONE, ET_EXEC, ET_DYN).
    uint16_t machine;   ///< Architecture (e.g. 0x3E for x86_64).
    uint32_t version;   ///< ELF version (must be 1).
    uint64_t entry;     ///< Virtual address of entry point.
    uint64_t phoff;     ///< Program header table offset.
    uint64_t shoff;     ///< Section header table offset.
    uint32_t flags;     ///< Processor-specific flags.
    uint16_t ehsize;    ///< ELF header size (64).
    uint16_t phentsize; ///< Size of one program header entry.
    uint16_t phnum;     ///< Number of program header entries.
    uint16_t shentsize; ///< Size of one section header entry.
    uint16_t shnum;     ///< Number of section header entries.
    uint16_t shstrndx;  ///< Section header string table index.

    ELF64Header() = default;
} __attribute__((packed));

/// @brief ELF64 program header (segment descriptor).
struct ELF64ProgramHeader {
    uint32_t type;   ///< Segment type (PT_NULL, PT_LOAD, PT_DYNAMIC, etc.).
    uint32_t flags;  ///< Segment flags (PF_R, PF_W, PF_X).
    uint64_t offset; ///< Offset in file.
    uint64_t vaddr;  ///< Virtual address to load at.
    uint64_t paddr;  ///< Physical address (usually same as vaddr).
    uint64_t filesz; ///< Size in file.
    uint64_t memsz;  ///< Size in memory (zero-padded if > filesz).
    uint64_t align;  ///< Alignment constraint.
} __attribute__((packed));

/// Program header type constants.
enum : uint8_t {
    PT_NULL = 0,    ///< Unused entry.
    PT_LOAD = 1,    ///< Loadable segment.
    PT_DYNAMIC = 2, ///< Dynamic linking info.
    PT_INTERP = 3,  ///< Interpreter path.
    PT_NOTE = 4,    ///< Auxiliary info.
    PT_PHDR = 6,    ///< Program header table itself.
};

/// Program header flag constants.
enum : uint8_t {
    PF_X = 1, ///< Execute permission.
    PF_W = 2, ///< Write permission.
    PF_R = 4, ///< Read permission.
};

/// ELF header type constants.
enum : uint8_t {
    ET_NONE = 0, ///< No file type.
    ET_EXEC = 2, ///< Executable.
    ET_DYN = 3,  ///< Shared object / PIE.
};

/// @brief Validate an ELF64 header (magic, class, endianness, version).
/// @return true if the header is valid.
bool validate_header(const ELF64Header *hdr);
/// @brief Load an ELF binary into a new task.
/// @param hdr Pointer to the ELF header.
/// @param data Raw ELF file data.
/// @return New TaskControlBlock, or nullptr on failure.
TaskControlBlock *load(const ELF64Header *hdr, const uint8_t *data);

/// @brief Replace the current task's address space with an ELF binary (exec).
/// @param regs Register state to update with new entry point and stack.
/// @return true on success.
bool exec_into_current(const ELF64Header *hdr, const uint8_t *data,
                       const char *const *argv, const char *const *envp,
                       uint64_t *regs);

} // namespace elf
} // namespace kernel
