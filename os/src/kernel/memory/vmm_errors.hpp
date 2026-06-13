/// @file vmm_errors.hpp
/// @brief Virtual Memory Manager error codes and string lookup.

#pragma once

#include <types.hpp>
#include <assert.hpp>

namespace kernel::errors {

#define VMM_ERROR_CODES \
    X(OK,             0,  "OK") \
    X(PAGE_ALLOC,     1,  "Failed to allocate page table page") \
    X(PML4_ALLOC,     2,  "Failed to allocate PML4 page for clone") \
    X(INVALID_ADDR,   3,  "Invalid virtual address") \
    X(NOT_MAPPED,     4,  "Virtual address not mapped")

enum VmmError : uint64_t {
#define X(name, num, msg) VMM_ERR_##name = num,
    VMM_ERROR_CODES
#undef X
};

/// @brief Return a human-readable string for a VMM error code.
template<>
inline const char* error_string(VmmError e) {
    switch (e) {
#define X(name, num, msg) case VMM_ERR_##name: return msg;
    VMM_ERROR_CODES
#undef X
    }
    return "Unknown VMM error";
}

} // namespace kernel::errors
