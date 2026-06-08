#include <syscall.h>
#include <unistd.h>

void exit(int status) {
    _exit(status);
}

void abort(void) {
    for (;;) asm volatile("pause");
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

#define HEAP_PHYS 0x60000000
#define HEAP_SIZE (1024 * 1024)

static unsigned long heap_next = HEAP_PHYS;
static int heap_inited = 0;

static void heap_init(void) {
    if (heap_inited) return;
    heap_next = HEAP_PHYS;
    heap_inited = 1;
}

void* malloc(size_t size) {
    if (!size) size = 1;
    heap_init();
    unsigned long old = heap_next;
    unsigned long new_next = old + size;
    if (new_next > HEAP_PHYS + HEAP_SIZE) return 0;
    heap_next = new_next;
    return (void*)old;
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
