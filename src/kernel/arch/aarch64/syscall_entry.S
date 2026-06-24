.section .text
.globl syscall_entry
.type syscall_entry, @function

// AArch64 syscall entry via SVC #0
// On entry:
//   X0 = syscall number
//   X1-X4 = args
//   EL0t mode
//
// We save registers, call the C handler, and return.
syscall_entry:
    // Save return address and frame
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    // Save X0-X4 (args + syscall number)
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    str     x4, [sp, #-16]!

    // Call kernel syscall handler: syscall_handler(number, arg0, arg1, arg2, arg3, regs)
    // X0-X5 are already set up by SVC caller
    // We need to pass the register save area as the last argument
    mov     x5, sp
    bl      syscall_handler

    // Restore and return
    ldr     x4, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    ldp     x29, x30, [sp], #16
    eret
