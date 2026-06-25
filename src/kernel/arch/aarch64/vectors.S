.section .vectors, "ax"
.globl vectors
.globl exception_entry
.align 12

// AArch64 exception vector table (16 entries, each 0x80 apart)
// Entries 0-3:  Current EL with SP0
// Entries 4-7:  Current EL with SPx
// Entries 8-11: Lower EL using AArch64
// Entries 12-15: Lower EL using AArch32

vectors:
exception_entry:
    // Current EL with SP0
    .align 7
    b       default_exception  // Sync
    .align 7
    b       default_exception  // IRQ
    .align 7
    b       default_exception  // FIQ
    .align 7
    b       default_exception  // SError

    // Current EL with SPx (kernel runs in EL1h, so this is the active set)
    .align 7
    b       default_exception  // Sync (EL1t sync — unused, kernel uses EL1h)
    .align 7
    b       el1_irq            // IRQ
    .align 7
    b       default_exception  // FIQ
    .align 7
    b       default_exception  // SError

    // Lower EL using AArch64
    .align 7
    b       default_exception  // Sync (SVC from EL0 → syscall — TODO)
    .align 7
    b       el0_irq            // IRQ while at EL0
    .align 7
    b       default_exception  // FIQ
    .align 7
    b       default_exception  // SError

    // Lower EL using AArch32
    .align 7
    b       default_exception  // Sync
    .align 7
    b       default_exception  // IRQ
    .align 7
    b       default_exception  // FIQ
    .align 7
    b       default_exception  // SError

.section .text
.type default_exception, @function
default_exception:
1:  wfe
    b       1b

// IRQ handler — delegates to C code for GIC-version-aware IAR/EOIR
.type el1_irq, @function
el1_irq:
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!
    bl      handle_gic_irq
    ldp     x29, x30, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret

.type el0_irq, @function
el0_irq:
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!
    bl      handle_gic_irq
    ldp     x29, x30, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret
