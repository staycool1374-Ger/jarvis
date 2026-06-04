/// @file error.hpp
/// @brief Kernel error codes and result-or-error wrapper.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Kernel error codes returned by various operations.
enum class Error : uint64_t {
    OK = 0,
    OOM,
    INVALID_ARG,
    NOT_FOUND,
    ALREADY_EXISTS,
    TIMEOUT,
    BUSY,
    NOT_IMPLEMENTED,
    IO_ERROR,
    CORRUPTED,
};

/// @brief Wraps a value or an error (similar to Rust's Result).
/// @tparam T The success value type.
template<typename T>
struct ErrorOr {
    T value;
    Error error;

    ErrorOr(T val) : value(val), error(Error::OK) {}
    ErrorOr(Error err) : value{}, error(err) {}

    bool ok() const { return error == Error::OK; }
    T& operator*() { return value; }
    const T& operator*() const { return value; }
};

/// @brief Specialization of ErrorOr for void (error-only result).
template<>
struct ErrorOr<void> {
    Error error;
    ErrorOr() : error(Error::OK) {}
    ErrorOr(Error err) : error(err) {}
    bool ok() const { return error == Error::OK; }
};

} // namespace kernel
