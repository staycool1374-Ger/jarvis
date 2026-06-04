/// @file types.hpp
/// @brief Fundamental type definitions for the kernel (no standard library).

#pragma once

using uint8_t   = unsigned char;
using uint16_t  = unsigned short;
using uint32_t  = unsigned int;
using uint64_t  = unsigned long long;

using int8_t    = signed char;
using int16_t   = signed short;
using int32_t   = signed int;
using int64_t   = signed long long;

using size_t    = uint64_t;
using uintptr_t = uint64_t;
using intptr_t  = int64_t;

using nullptr_t = decltype(nullptr);

/// @brief User-defined literal for kibibytes (multiplies by 1024).
static constexpr size_t operator""_KiB(unsigned long long v) { return v * 1024; }
/// @brief User-defined literal for mebibytes (multiplies by 1024^2).
static constexpr size_t operator""_MiB(unsigned long long v) { return v * 1024 * 1024; }
