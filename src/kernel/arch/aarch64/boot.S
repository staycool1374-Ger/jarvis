.section .boot.text, "ax"
.globl _start
_start:
    // Check current EL
    mrs     x0, CurrentEl
    and     x0, x0, #0xC
    cmp     x0, #0x8          // EL2?
    b.ne    1f

    // Drop from EL2 to EL1
    mov     x0, #(1 << 31)    // HCR_EL2.RW = AArch64
    msr     hcr_el2, x0

    // SPSR_EL2: EL1h with all exceptions masked
    mov     x0, #0x3C5
    msr     spsr_el2, x0

    // Set EL1 entry point
    adr     x0, 1f
    msr     elr_el2, x0
    eret

1:  // Now in EL1
    // Set VBAR_EL1 to our vector table
    adr     x0, vectors
    msr     vbar_el1, x0

    // Configure FPEN in CPACR_EL1
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)  // FPEN = 3 (no traps)
    msr     cpacr_el1, x0

    // Set up a minimal page table for identity map + higher half
    // Page table setup is deferred to C code — for now just enable
    // cache coherency and set up stack

    // Set up boot stack (4 pages = 16 KiB)
    ldr     x0, =_boot_stack_end
    mov     sp, x0

    // Jump to higherhalf_entry (C entry point)
    b       higherhalf_entry

.section .boot.bss, "aw", @nobits
.align 12
_boot_stack:
    .space  16384
_boot_stack_end:
