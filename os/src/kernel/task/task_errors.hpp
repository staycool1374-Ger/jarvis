/// @file task_errors.hpp
/// @brief Task module error codes and string lookup.

#pragma once

#include <types.hpp>
#include <assert.hpp>

namespace kernel::errors {

#define TASK_ERROR_CODES \
    X(OK,            0,  "OK") \
    X(OOM,           1,  "Task creation failed, out of memory") \
    X(TABLE_FULL,    2,  "Task creation failed, maximum of MAX_TASKS reached") \
    X(STACK_ALLOC,   3,  "Kernel stack allocation failed") \
    X(USTACK_ALLOC,  4,  "User stack allocation failed") \
    X(PML4_CLONE,    5,  "PML4 clone failed") \
    X(NOT_FOUND,     6,  "Task not found") \
    X(INVALID_ARG,   7,  "Invalid argument") \
    X(INVALID_STATE, 8,  "Invalid task state")

enum TaskError : uint64_t {
#define X(name, num, msg) TASK_ERR_##name = num,
    TASK_ERROR_CODES
#undef X
};

/// @brief Return a human-readable string for a task error code.
template<>
inline const char* error_string(TaskError error) {
    switch (error) {
#define X(name, num, msg) case TASK_ERR_##name: return msg;
    TASK_ERROR_CODES
#undef X
    }
    return "Unknown task error";
}

} // namespace kernel::errors
