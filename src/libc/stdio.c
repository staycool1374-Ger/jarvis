#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char* s) {
    write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

int getchar(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return -1;
}

static void print_hex(unsigned long n, char** p, char* end) {
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i = 16;
    buf[i] = '\0';
    if (n == 0) buf[--i] = '0';
    else while (n && i) { buf[--i] = hex[n & 0xF]; n >>= 4; }
    while (buf[i] && *p < end) { *(*p)++ = buf[i++]; }
}

static void print_dec(unsigned long n, char** p, char* end) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (n == 0) buf[--i] = '0';
    else while (n && i) { buf[--i] = '0' + (n % 10); n /= 10; }
    while (buf[i] && *p < end) { *(*p)++ = buf[i++]; }
}

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    char* p = buf;
    char* end = buf + n - 1;
    if (n == 0) end = buf;

    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        ++fmt;
        int zero_pad = 0;
        int width = 0;
        if (*fmt == '0') {
            zero_pad = 1;
            ++fmt;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }
        long lval = 0;
        unsigned long ulval = 0;
        int is_long = 0;
        while (*fmt == 'l') { is_long = 1; ++fmt; }

        switch (*fmt) {
        case 'd':
        case 'i':
            if (is_long) lval = va_arg(ap, long);
            else lval = (long)va_arg(ap, int);
            if (lval < 0) { if (p < end) *p++ = '-'; ulval = (unsigned long)(-lval); }
            else ulval = (unsigned long)lval;
            print_dec(ulval, &p, end);
            break;
        case 'u':
            ulval = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            print_dec(ulval, &p, end);
            break;
        case 'x':
        case 'X':
            ulval = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            print_hex(ulval, &p, end);
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && p < end) *p++ = *s++;
            break;
        }
        case 'c':
            if (p < end) *p++ = (char)va_arg(ap, int);
            break;
        case 'p':
            if (p < end) *p++ = '0';
            if (p < end) *p++ = 'x';
            print_hex((unsigned long)va_arg(ap, void*), &p, end);
            break;
        case '%':
            if (p < end) *p++ = '%';
            break;
        default:
            if (p < end) *p++ = '%';
            if (p < end) *p++ = *fmt;
            break;
        }
        ++fmt;
    }
    *p = '\0';
    return (int)(p - buf);
}

int snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(STDOUT_FILENO, buf, (size_t)r);
    return r;
}
