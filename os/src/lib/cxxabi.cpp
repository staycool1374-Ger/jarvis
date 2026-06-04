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
    while (1) { asm volatile("hlt"); }
}

extern "C" void __stack_chk_fail() {
    while (1) { asm volatile("hlt"); }
}

extern "C" void _Unwind_Resume() {
    while (1) { asm volatile("hlt"); }
}

extern "C" void __gxx_personality_v0() {
}

static void div128_trap() {
    asm volatile("hlt");
    while (1) { asm volatile("hlt"); }
}

extern "C" void __udivti3() { div128_trap(); }
extern "C" void __umodti3() { div128_trap(); }
extern "C" void __divti3() { div128_trap(); }
extern "C" void __modti3() { div128_trap(); }

extern "C" void __chkstk() {
}

extern "C" void __chkstk_ms() {
}
