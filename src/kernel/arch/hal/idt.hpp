#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
    IDTEntry() = default;
} __attribute__((packed));

struct IDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

enum class InterruptVector : uint8_t {
    DIV_ZERO       = 0,
    DEBUG          = 1,
    NMI            = 2,
    BREAKPOINT     = 3,
    OVERFLOW       = 4,
    BOUND_RANGE    = 5,
    INVALID_OPCODE = 6,
    DEVICE_NA      = 7,
    DOUBLE_FAULT   = 8,
    INVALID_TSS    = 10,
    SEGMENT_NP     = 11,
    STACK_SEG_FAULT = 12,
    GP_FAULT       = 13,
    PAGE_FAULT     = 14,
    FPU_ERROR      = 16,
    ALIGN_CHECK    = 17,
    MACHINE_CHECK  = 18,
    SIMD_ERROR     = 19,
    VIRT_ERROR     = 20,
    TIMER          = 32,
    KEYBOARD       = 33,
    SYSCALL        = 0x80,
};

using ISRHandler = void (*)(uint64_t vector, uint64_t error_code, uint64_t rip);

class IDT {
public:
    static void init();
    static void load();
    static void register_handler(InterruptVector vec, ISRHandler handler);
    static void register_handler_raw(uint8_t vec, ISRHandler handler);
    static void handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t rip);
    static const IDTEntry& entry(uint8_t vec);
    static bool has_handler(size_t vec);

private:
    static constexpr size_t NUM_ENTRIES = 256;
    static IDTEntry entries_[NUM_ENTRIES];
    static IDTDescriptor desc_;
    static ISRHandler handlers_[NUM_ENTRIES];
};

extern "C" void isr_entry();

#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)

enum class InterruptVector : uint8_t {
    TIMER          = 0,
    KEYBOARD       = 1,
    SYSCALL        = 2,
};

using ISRHandler = void (*)(uint64_t vector, uint64_t error_code, uint64_t rip);

class IDT {
public:
    static void init();
    static void load();
    static void register_handler(InterruptVector vec, ISRHandler handler);
    static void register_handler_raw(uint8_t vec, ISRHandler handler);
    static void handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t rip);

private:
    static constexpr size_t NUM_ENTRIES = 64;
    static ISRHandler handlers_[NUM_ENTRIES];
};

#else
#  error "HAL: no idt implementation for this architecture"
#endif

} // namespace arch
