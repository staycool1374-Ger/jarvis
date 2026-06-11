/// @file pmm_errors.hpp
/// @brief Physical Memory Manager error codes and string lookup.

#pragma once

#include <types.hpp>
#include <assert.hpp>

namespace kernel::errors {

#define PMM_ERROR_CODES \
    X(OK,         0,  "OK") \
    X(OOM,        1,  "Out of memory — no free physical pages") \
    X(USER_OOM,   2,  "Out of memory — no free user physical pages") \
    X(TABLE_OOM,  3,  "Out of memory — page table pool exhausted") \
    X(INVALID,    4,  "Invalid physical address")

enum PmmError : uint64_t {
#define X(name, num, msg) PMM_ERR_##name = num,
    PMM_ERROR_CODES
#undef X
};

template<>
inline const char* error_string(PmmError e) {
    switch (e) {
#define X(name, num, msg) case PMM_ERR_##name: return msg;
    PMM_ERROR_CODES
#undef X
    }
    return "Unknown PMM error";
}

} // namespace kernel::errors
