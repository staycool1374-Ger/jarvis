#include <types.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

extern "C" {

#define MAX_FUNCTIONS 4096

static uint64_t called_functions[MAX_FUNCTIONS / 64];
static uint64_t func_addrs[MAX_FUNCTIONS];
static uint32_t func_count = 0;

static uint32_t __attribute__((no_instrument_function))
find_or_add_func(uint64_t func_addr) {
    for (uint32_t i = 0; i < func_count; i++) {
        if (func_addrs[i] == func_addr)
            return i;
    }
    if (func_count < MAX_FUNCTIONS) {
        func_addrs[func_count] = func_addr;
        return func_count++;
    }
    return 0;
}

void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void *func, void *caller) {
    (void)caller;
    uint64_t addr = reinterpret_cast<uint64_t>(func);
    uint32_t idx = find_or_add_func(addr);
    called_functions[idx / 64] |= (1ULL << (idx % 64));
}

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void *func, void *caller) {
    (void)func;
    (void)caller;
}

void __attribute__((no_instrument_function))
gcov_flush_to_serial() {
    uint8_t magic[4] = {'F', 'U', 'N', 'C'};
    for (int i = 0; i < 4; ++i) {
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, magic[i]);
    }

    uint32_t total = func_count;
    for (int i = 0; i < 4; ++i) {
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, (total >> (i * 8)) & 0xFF);
    }

    for (uint32_t i = 0; i < func_count; ++i) {
        uint64_t addr = func_addrs[i];
        uint32_t called = (called_functions[i / 64] >> (i % 64)) & 1;
        for (int j = 0; j < 8; ++j) {
            while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
            arch::outb(arch::COM1, (addr >> (j * 8)) & 0xFF);
        }
        while ((arch::inb(arch::COM1 + 5) & 0x20) == 0);
        arch::outb(arch::COM1, called & 0xFF);
    }
}

} // extern "C"