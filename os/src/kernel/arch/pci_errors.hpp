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

enum PciError : uint64_t {
#define X(name, num, msg) PCI_ERR_##name = num,
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
