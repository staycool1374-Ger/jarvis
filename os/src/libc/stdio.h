#pragma once

#include <sys/types.h>
#include <stdarg.h>

enum {
    _IOFBF = 0,
    _IOLBF = 1,
    _IONBF = 2,
};

int printf(const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t n, const char* fmt, ...);
int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap);
int puts(const char* s);
int putchar(int c);
int getchar(void);
