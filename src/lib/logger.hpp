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

/// @file logger.hpp
/// @brief Kernel logger subsystem with verbosity tiers and color output.

#pragma once

#include <types.hpp>

// NOLINTBEGIN(bugprone-reserved-identifier)
using __va_list = __builtin_va_list;
// NOLINTEND(bugprone-reserved-identifier)
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

// NOLINTBEGIN(bugprone-narrowing-conversions)
namespace kernel {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4,
};

class Logger {
public:
    static void init();

    static void set_level(LogLevel level);

    static void debug(const char* fmt, ...);
    static void info(const char* fmt, ...);
    static void warn(const char* fmt, ...);
    static void error(const char* fmt, ...);
    static void fatal(const char* fmt, ...);

    static void vprint(LogLevel level, const char* fmt, __va_list args);
    static void vprint_raw(const char* fmt, __va_list args);
    static void raw_write(const char* s);
    static void print_hex(uint64_t v);
    static void print_dec(uint64_t v);

private:
    static constinit LogLevel current_level_;
    static constinit bool initialized_;

    static void putchar(char c);
    static void puts(const char* s);
    static const char* level_prefix(LogLevel level);
    static const char* level_ansi(LogLevel level);
};

} // namespace kernel
// NOLINTEND(bugprone-narrowing-conversions)
