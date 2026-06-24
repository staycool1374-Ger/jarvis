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

    ldr     x0, =_boot_stack_end
    mov     sp, x0

    ldr     x0, =0x9000000
    mov     w1, #'1'
    str     w1, [x0]

    // === Build single-level page table at PA 0x40000000 ===
    // TTBR points directly to L1 table (TCR.T0SZ=25 starts walk at L1)
    mov     x9, #0x40000000             // x9 = L1 base (PA 0x40000000)

    // Clear 4 entries needed
    str     xzr, [x9]                   // L1[0] = 0
    str     xzr, [x9, #8]               // L1[1] = 0
    str     xzr, [x9, #0x10]            // L1[2] = 0
    str     xzr, [x9, #0x18]            // L1[3] = 0

    // Write 1GB block descriptors for first 4GB
    // (VA 0-4GB → PA 0-4GB)
    mov     x12, #0x321
    // 0-1GB
    str     x12, [x9]
    // 1-2GB
    mov     x13, #0x40000000
    orr     x13, x13, x12
    str     x13, [x9, #8]
    // 2-3GB
    mov     x13, #0x80000000
    orr     x13, x13, x12
    str     x13, [x9, #0x10]
    // 3-4GB
    mov     x13, #0xC0000000
    orr     x13, x13, x12
    str     x13, [x9, #0x18]

    dsb     sy
    isb

    // Read back L1[1] low nibble
    ldr     x14, [x9, #8]
    ldr     x0, =0x9000000
    and     w1, w14, #0xF
    add     w1, w1, #'0'
    str     w1, [x0]

    ldr     x0, =0x9000000
    mov     w1, #'A'
    str     w1, [x0]

    // MAIR
    mov     x8, #0xFF
    msr     mair_el1, x8

    // TCR: T0SZ=25 → 39-bit VA, walk starts at L1
    // T1SZ=25, EPD1=1, TG0=4KB, all else 0
    ldr     x8, =0x990019
    msr     tcr_el1, x8

    // TTBR0 = L1 base (0x40000000)
    msr     ttbr0_el1, x9
    msr     ttbr1_el1, x9

    dsb     sy
    isb

    ldr     x0, =0x9000000
    mov     w1, #'B'
    str     w1, [x0]

    // Enable MMU
    mov     x0, #1
    msr     sctlr_el1, x0
    isb

    // Write 'C'
    ldr     x0, =0x9000000
    mov     w1, #'C'
    str     w1, [x0]

    ldr     x1, =0xFFFF800000000000
    add     sp, sp, x1

    adr     x0, 2f
    add     x0, x0, x1
    br      x0

2:
    mov     x0, #0
    mov     x1, #0
    bl      higherhalf_entry

3:  wfe
    b       3b

    .pool

.section .boot.data, "aw"
.balign 4096
.globl _boot_pgtables
_boot_pgtables:
    .space  0x8000, 0
_boot_pgtables_end:

.balign 4096
_boot_stack:
    .space  16384
_boot_stack_end:
