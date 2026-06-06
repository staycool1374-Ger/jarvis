/// @file logger.hpp
/// @brief Kernel logger subsystem with verbosity tiers and color output.

#pragma once

#include <types.hpp>

using __va_list = __builtin_va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

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
    static void raw_write(const char* s);
    static void print_hex(uint64_t v);
    static void print_dec(uint64_t v);

private:
    static LogLevel current_level_;
    static bool initialized_;

    static void putchar(char c);
    static void puts(const char* s);
    static const char* level_prefix(LogLevel level);
    static const char* level_ansi(LogLevel level);
};

} // namespace kernel
