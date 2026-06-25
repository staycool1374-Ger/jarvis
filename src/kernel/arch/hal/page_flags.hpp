#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Architecture-independent page flags for page table operations.
/// Used by VMM and ArchPageTable to build page table entries.
struct PageFlags {
    static constexpr uint64_t PRESENT         = 1ULL << 0;  // Page is present
    static constexpr uint64_t WRITE           = 1ULL << 1;  // Writable
    static constexpr uint64_t USER            = 1ULL << 2;  // User-accessible (EL0)
    static constexpr uint64_t NX              = 1ULL << 3;  // No-execute
    static constexpr uint64_t CACHE_DISABLED  = 1ULL << 4;  // Uncached/Device memory
    static constexpr uint64_t HUGE            = 1ULL << 7;  // 2MB/1GB page (detected via level)
};

} // namespace kernel