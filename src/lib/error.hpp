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
