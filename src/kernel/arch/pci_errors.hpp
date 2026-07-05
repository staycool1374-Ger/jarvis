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

/// @file pci_errors.hpp
/// @brief PCI error codes and string lookup.

#pragma once

#include <types.hpp>
#include <assert.hpp>

namespace kernel::errors {

#define PCI_ERROR_CODES \
    X(OK,             0,  "OK") \
    X(NODEV,          1,  "No PCI device at this BDF") \
    X(NOMEM,          2,  "PCI memory allocation failed") \
    X(INVALID_BAR,    3,  "Invalid or uninitialized BAR") \
    X(NOT_FOUND,      4,  "PCI device not found")

// NOLINTNEXTLINE(performance-enum-size)
enum PciError : uint64_t {
#define X(name, num, msg) PCI_ERR_##name = (num),
    PCI_ERROR_CODES
#undef X
};

template<>
inline const char* error_string(PciError e) {
    switch (e) {
#define X(name, num, msg) case PCI_ERR_##name: return msg;
    PCI_ERROR_CODES
#undef X
    }
    return "Unknown PCI error";
}

} // namespace kernel::errors
