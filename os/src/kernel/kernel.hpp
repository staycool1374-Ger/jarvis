/// @file kernel.hpp
/// @brief Kernel entry points and global declarations.

#pragma once

#include <types.hpp>

extern "C" {
/// @brief Entry point after transitioning to the higher half.
/// @param magic   Multiboot2 magic value.
/// @param mb_info Multiboot2 info structure pointer.
void higherhalf_entry(uint64_t magic, uint64_t mb_info);
/// @brief Kernel panic handler (noreturn).
/// @param msg Panic message string.
void panic(const char* msg) __attribute__((noreturn));
/// @brief C-level interrupt handler dispatcher.
/// @param vector     Interrupt vector number.
/// @param error_code CPU error code (0 if none).
    /// @param rip        Instruction pointer at time of interrupt.
    /// @param regs       Pointer to saved register array (GPRs).
    void handle_interrupt_c(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t* regs);
}

/// @brief Kernel stack base (bottom address).
extern uint8_t kernel_stack[];
