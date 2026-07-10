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

#include <logger.hpp>
#include <kernel/arch/serial.hpp>
#include <kernel/log/ring_buffer.hpp>

namespace kernel {

constinit LogLevel Logger::current_level_ = LogLevel::INFO;
constinit bool Logger::initialized_ = false;

void Logger::init() {
    if (initialized_) return;
    arch::Serial::init();
    initialized_ = true;
    info("Logger initialized");
}

void Logger::set_level(LogLevel level) {
    current_level_ = level;
}

void Logger::putchar(char c) {
    arch::Serial::putchar(c);
    if (initialized_) {
        kernel::log::g_klog.putchar(c);
    }
}

void Logger::puts(const char* s) {
    arch::Serial::puts(s);
    if (initialized_) {
        kernel::log::g_klog.puts(s);
    }
}

void Logger::print_hex(uint64_t v) {
    char buf[17];
    int pos = 16;
    buf[16] = '\0';
    if (v == 0) { puts("0x0"); return; }
    while (v > 0) {
        buf[--pos] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }
    puts("0x");
    puts(buf + pos);
}

void Logger::print_dec(uint64_t v) {
    char buf[21];
    int pos = 20;
    buf[20] = '\0';
    if (v == 0) { putchar('0'); return; }
    while (v > 0) {
        buf[--pos] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    puts(buf + pos);
}

const char* Logger::level_ansi(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG: return "\033[90m";
    case LogLevel::INFO:  return "\033[37m";
    case LogLevel::WARN:  return "\033[1;33m";
    case LogLevel::ERROR: return "\033[1;31m";
    case LogLevel::FATAL: return "\033[1;41;37m";
    default:              return "\033[37m";
    }
}

const char* Logger::level_prefix(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG: return "[DEBUG]";
    case LogLevel::INFO:  return "[INFO] ";
    case LogLevel::WARN:  return "[WARN] ";
    case LogLevel::ERROR: return "[ERROR]";
    case LogLevel::FATAL: return "[FATAL]";
    default:              return "[?]";
    }
}

void Logger::vprint(LogLevel level, const char* fmt, __va_list args) {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(current_level_)) return;
    if (!initialized_) return;

    puts(level_ansi(level));
    puts(level_prefix(level));
    puts(" ");

    while (*fmt) {
        if (*fmt == '%') {
            ++fmt;
            if (*fmt == 'l') ++fmt;
            switch (*fmt) {
            case 's': {
                const char* s = __builtin_va_arg(args, const char*);
                puts(s ? s : "(null)");
                break;
            }
            case 'x': {
                uint64_t v = __builtin_va_arg(args, uint64_t);
                print_hex(v);
                break;
            }
            case 'd':
            case 'u': {
                uint64_t v = __builtin_va_arg(args, uint64_t);
                print_dec(v);
                break;
            }
            case 'c': {
                int c = __builtin_va_arg(args, int);
                putchar(static_cast<char>(c));
                break;
            }
            case '%': {
                putchar('%');
                break;
            }
            default:
                putchar('%');
                putchar(*fmt);
                break;
            }
            ++fmt;
        } else {
            putchar(*fmt++);
        }
    }
    puts("\033[0m\n");
}

void Logger::vprint_raw(const char* fmt, __va_list args) {
    if (!initialized_) return;
    while (*fmt) {
        if (*fmt == '%') {
            ++fmt;
            if (*fmt == 'l') ++fmt;
            switch (*fmt) {
            case 's': {
                const char* s = __builtin_va_arg(args, const char*);
                puts(s ? s : "(null)");
                break;
            }
            case 'x': {
                uint64_t v = __builtin_va_arg(args, uint64_t);
                print_hex(v);
                break;
            }
            case 'd':
            case 'u': {
                uint64_t v = __builtin_va_arg(args, uint64_t);
                print_dec(v);
                break;
            }
            case 'c': {
                int c = __builtin_va_arg(args, int);
                putchar(static_cast<char>(c));
                break;
            }
            case '%': {
                putchar('%');
                break;
            }
            default:
                putchar('%');
                putchar(*fmt);
                break;
            }
            ++fmt;
        } else {
            putchar(*fmt++);
        }
    }
}

void Logger::debug(const char* fmt, ...) {
    __va_list args;
    va_start(args, fmt);
    vprint(LogLevel::DEBUG, fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    __va_list args;
    va_start(args, fmt);
    vprint(LogLevel::INFO, fmt, args);
    va_end(args);
}

void Logger::warn(const char* fmt, ...) {
    __va_list args;
    va_start(args, fmt);
    vprint(LogLevel::WARN, fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    __va_list args;
    va_start(args, fmt);
    vprint(LogLevel::ERROR, fmt, args);
    va_end(args);
}

void Logger::fatal(const char* fmt, ...) {
    __va_list args;
    va_start(args, fmt);
    vprint(LogLevel::FATAL, fmt, args);
    va_end(args);
}

void Logger::raw_write(const char* s) {
    puts(s);
}

} // namespace kernel
