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

    // Current EL with SPx
    .align 7
    b       default_exception  // Sync
    .align 7
    b       default_exception  // IRQ
    .align 7
    b       default_exception  // FIQ
    .align 7
    b       default_exception  // SError

    // Lower EL using AArch64
    .align 7
    b       default_exception  // Sync
    .align 7
    b       default_exception  // IRQ
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
    // Halt loop — no exception handling yet
1:  wfe
    b       1b
