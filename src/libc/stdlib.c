#include <syscall.h>
#include <unistd.h>

void exit(int status) {
    _exit(status);
}

void abort(void) {
    for (;;) __builtin_trap();
}

int atoi(const char* s) {
    int n = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '-') { sign = -1; ++s; }
    else if (*s == '+') ++s;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        ++s;
    }
    return sign * n;
}

long atol(const char* s) {
    return (long)atoi(s);
}

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

int brk(void* addr) {
    void* ret = sys_brk(addr);
    return (ret == addr || addr == 0) ? 0 : -1;
}

void* sbrk(long increment) {
    void* old = sys_brk(0);
    if (old == (void*)-1) return (void*)-1;
    if (increment == 0) return old;
    void* new_brk = (void*)((unsigned long)old + increment);
    void* ret = sys_brk(new_brk);
    return (ret == new_brk) ? old : (void*)-1;
}

void* malloc(size_t size) {
    if (!size) size = 1;
    return sbrk(size);
}

void free(void* ptr) {
    (void)ptr;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (!total) return 0;
    void* p = malloc(total);
    if (p) {
        unsigned char* cp = (unsigned char*)p;
        for (size_t i = 0; i < total; ++i) cp[i] = 0;
    }
    return p;
}

void* realloc(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return 0;
}
