/// @file idt.hpp
/// @brief Interrupt Descriptor Table structures and handler registration.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief A single 64-bit IDT entry (16 bytes).
struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

/// @brief The IDT descriptor structure for the LIDT instruction.
struct IDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/// @brief Named interrupt vector numbers for CPU exceptions and IRQs.
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

/// @brief Type for interrupt service routine callbacks.
using ISRHandler = void (*)(uint64_t vector, uint64_t error_code, uint64_t rip);

/// @brief Manages the Interrupt Descriptor Table.
/// @note Supports up to 256 entries with pluggable handlers.
class IDT {
public:
    /// @brief Initialises all IDT entries with default handlers.
    static void init();
    /// @brief Loads the IDT using the LIDT instruction.
    static void load();

    /// @brief Registers a handler for a specific interrupt vector.
    /// @param vec     The interrupt vector.
    /// @param handler The handler function to register.
    static void register_handler(InterruptVector vec, ISRHandler handler);
    /// @brief Dispatches an interrupt to the registered handler.
    /// @param vector     The vector number.
    /// @param error_code CPU error code.
    /// @param rip        Instruction pointer at interrupt.
    static void handle_interrupt(uint64_t vector, uint64_t error_code, uint64_t rip);

private:
    static constexpr size_t NUM_ENTRIES = 256;
    static IDTEntry entries_[NUM_ENTRIES];
    static IDTDescriptor desc_;
    static ISRHandler handlers_[NUM_ENTRIES];
};

/// @brief Assembly-level ISR entry point (saves registers, calls C handler).
extern "C" void isr_entry();

} // namespace arch
