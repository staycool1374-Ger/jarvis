/// @file version.hpp
/// @brief Kernel version information.

#pragma once

#include <types.hpp>

/// @brief Kernel version — single source of truth.
/// Format: "vmajor.minor.patch"
#define KERNEL_VERSION_STRING "v0.2.16-dev"

namespace kernel {

/// @brief Kernel version constants and string formatting.
/// @note Major/minor/patch and build timestamp are defined at compile time.
struct Version {
    static constexpr unsigned major = 0;
    static constexpr unsigned minor = 2;
    static constexpr unsigned patch = 16;
    static constexpr const char* stage = "dev";

    static const char* string();
    static const char* build_date();
    static const char* build_time();
    static const char* full_string();
};

} // namespace kernel