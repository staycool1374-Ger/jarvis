/// @file assert.hpp
/// @brief Debug assertion macros and the panic handler declaration.

#pragma once

#include <types.hpp>

/// @brief Triggers a kernel panic with a message (noreturn).
extern "C" void panic(const char* msg);

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            panic("ASSERT failed: " #cond " at " __FILE__ ":" \
                  __STRINGIFY(__LINE__)); \
        } \
    } while (0)

#define __STRINGIFY(x) #x
