#pragma once

#include <types.hpp>
#include <kernel/task/task.hpp>

namespace kernel {
namespace elf {

struct ELF64Header {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;

    ELF64Header() = default;
} __attribute__((packed));

struct ELF64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

enum : uint32_t {
    PT_NULL    = 0,
    PT_LOAD    = 1,
    PT_DYNAMIC = 2,
    PT_INTERP  = 3,
    PT_NOTE    = 4,
    PT_PHDR    = 6,
};

enum : uint32_t {
    PF_X = 1,
    PF_W = 2,
    PF_R = 4,
};

enum : uint16_t {
    ET_NONE = 0,
    ET_EXEC = 2,
    ET_DYN  = 3,
};

/// @brief Validate an ELF64 header (magic, class, endianness, version).
/// @return true if the header is valid.
bool validate_header(const ELF64Header* hdr);
/// @brief Load an ELF binary into a new task.
/// @param hdr Pointer to the ELF header.
/// @param data Raw ELF file data.
/// @return New TaskControlBlock, or nullptr on failure.
TaskControlBlock* load(const ELF64Header* hdr, const uint8_t* data);

/// @brief Replace the current task's address space with an ELF binary (exec).
/// @param regs Register state to update with new entry point and stack.
/// @return true on success.
bool exec_into_current(const ELF64Header* hdr, const uint8_t* data,
                       const char* const* argv, const char* const* envp,
                       uint64_t* regs);

} // namespace elf
} // namespace kernel
