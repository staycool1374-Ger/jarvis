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

/// @file string.hpp
/// @brief Memory and string utility functions (extern "C" for ABI compat).

#pragma once

#include <types.hpp>

// NOLINTBEGIN(bugprone-narrowing-conversions)
extern "C" {

/// @brief Fills a memory block with a byte value.
/// @param dest Pointer to the memory to fill.
/// @param ch   Byte value to set (truncated to unsigned char).
/// @param count Number of bytes to fill.
/// @return Pointer to dest.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline void* memset(void* dest, int ch, size_t count) {
    auto* d = static_cast<uint8_t*>(dest);
    uint8_t val = static_cast<uint8_t>(ch);
    for (size_t i = 0; i < count; ++i) d[i] = val;
    return dest;
}

/// @brief Copies memory from source to destination.
/// @param dest Destination pointer.
/// @param src  Source pointer.
/// @param count Number of bytes to copy.
/// @return Pointer to dest.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline void* memcpy(void* dest, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dest);
    auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return dest;
}

/// @brief Compares two memory blocks.
/// @param a First memory block.
/// @param b Second memory block.
/// @param count Number of bytes to compare.
/// @return 0 if equal, negative if a < b, positive if a > b.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline int memcmp(const void* a, const void* b, size_t count) {
    auto* pa = static_cast<const uint8_t*>(a);
    auto* pb = static_cast<const uint8_t*>(b);
    while (count--) {
        if (*pa != *pb) return *pa - *pb;
        ++pa; ++pb;
    }
    return 0;
}

/// @brief Returns the length of a null-terminated string.
/// @param s Null-terminated string.
/// @return Length excluding the null terminator.
inline size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) ++len;
    return len;
}

inline int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

inline int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        if (a[i] == '\0') return 0;
    }
    return 0;
}

inline char* strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) { dest[i] = src[i]; ++i; }
    while (i < n) { dest[i] = '\0'; ++i; }
    return dest;
}

inline size_t strlcpy(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return 0;
    size_t srclen = strlen(src);
    size_t copylen = (srclen < n - 1) ? srclen : n - 1;
    for (size_t i = 0; i < copylen; ++i) dest[i] = src[i];
    dest[copylen] = '\0';
    return srclen;
}

} // extern "C"
// NOLINTEND(bugprone-narrowing-conversions)
