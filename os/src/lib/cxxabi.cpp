#include <types.hpp>

extern "C" int __cxa_guard_acquire(uint64_t* g) {
    if (*g == 0) return 1;
    return 0;
}

extern "C" void __cxa_guard_release(uint64_t* g) {
    *g = 1;
}

extern "C" void __cxa_guard_abort(uint64_t* g) {
    (void)g;
}

extern "C" void __cxa_pure_virtual() {
    while (1) {}
}

extern "C" void __stack_chk_fail() {
    while (1) {}
}

extern "C" void _Unwind_Resume() {
    while (1) {}
}

extern "C" void __gxx_personality_v0() {
}

static void div128_trap() {
    while (1) {}
}

extern "C" void __udivti3() { div128_trap(); }
extern "C" void __umodti3() { div128_trap(); }
extern "C" void __divti3() { div128_trap(); }
extern "C" void __modti3() { div128_trap(); }

extern "C" void __chkstk() {
}

extern "C" void __chkstk_ms() {
}

extern "C" uint64_t g_user_access_recover_ip = 0;

extern "C" void* memset(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    while (n--) *p++ = static_cast<unsigned char>(c);
    return s;
}

extern "C" void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

extern "C" void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
