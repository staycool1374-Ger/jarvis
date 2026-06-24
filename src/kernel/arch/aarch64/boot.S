.section .boot.text, "ax"
.globl _start
.type _start, @function

_start:
    mrs     x0, CurrentEl
    and     x0, x0, #0xC
    cmp     x0, #0x8
    b.ne    1f
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    mov     x0, #0x3C5
    msr     spsr_el2, x0
    adr     x0, 1f
    msr     elr_el2, x0
    eret

1:
    adr     x0, vectors
    msr     vbar_el1, x0
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0

    // Set up stack (MMU off, physical address)
    ldr     x0, =_boot_stack_end
    mov     sp, x0

    // Write '1' to serial
    ldr     x0, =0x9000000
    mov     w1, #'1'
    str     w1, [x0]

    // Call C early init (sets up page tables, TCR, MAIR, TTBRs)
    bl      arch_aarch64_early_init

    // Write '2' to confirm C init returned
    ldr     x0, =0x9000000
    mov     w1, #'2'
    str     w1, [x0]

    // Dump TTBR0 to UART (low byte only for sanity)
    mrs     x2, ttbr0_el1
    ldr     x0, =0x9000000
    lsr     x1, x2, #4
    and     w1, w1, #0xF
    add     w1, w1, #'0'
    str     w1, [x0]

    // Enable MMU: read SCTLR, set M=1, write back
    mrs     x0, sctlr_el1
    orr     x0, x0, #1
    msr     sctlr_el1, x0
    isb

    // Write '3' after MMU enabled (via identity map)
    ldr     x0, =0x9000000
    mov     w1, #'3'
    str     w1, [x0]

    // Convert SP to higher half and branch
    ldr     x1, =0xFFFF800000000000
    add     sp, sp, x1

    adr     x0, 2f
    add     x0, x0, x1
    br      x0

2:  // Now in higher half
    mov     x0, #0
    mov     x1, #0
    bl      higherhalf_entry

3:  wfe
    b       3b

    .pool

.section .boot.bss, "aw", @nobits
.align 12
.globl _boot_pgtables_start
_boot_pgtables_start:
    .space  0x8000
_boot_pgtables_end:
.align 12
_boot_stack:
    .space  16384
_boot_stack_end:
