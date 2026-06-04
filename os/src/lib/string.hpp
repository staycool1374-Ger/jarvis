/// @file string.hpp
/// @brief Memory and string utility functions (extern "C" for ABI compat).

#pragma once

#include <types.hpp>

extern "C" {

/// @brief Fills a memory block with a byte value.
/// @param dest Pointer to the memory to fill.
/// @param ch   Byte value to set (truncated to unsigned char).
/// @param count Number of bytes to fill.
/// @return Pointer to dest.
inline void* memset(void* dest, int ch, size_t count) {
    auto* d = static_cast<uint8_t*>(dest);
    if (count >= 16) {
        asm volatile("rep stosb" : "+D"(d), "+c"(count) : "a"(static_cast<uint8_t>(ch)) : "memory");
    } else {
        for (size_t i = 0; i < count; ++i) d[i] = static_cast<uint8_t>(ch);
    }
    return dest;
}

/// @brief Copies memory from source to destination.
/// @param dest Destination pointer.
/// @param src  Source pointer.
/// @param count Number of bytes to copy.
/// @return Pointer to dest.
inline void* memcpy(void* dest, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dest);
    auto* s = static_cast<const uint8_t*>(src);
    if (count >= 16) {
        asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(count) : : "memory");
    } else {
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    }
    return dest;
}

/// @brief Compares two memory blocks.
/// @param a First memory block.
/// @param b Second memory block.
/// @param count Number of bytes to compare.
/// @return 0 if equal, negative if a < b, positive if a > b.
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

} // extern "C"
