.section .boot.text, "ax"
.globl _start
.type _start, @function

_start:
    // Save DTB address (x0 from bootloader) in x20
    mov     x20, x0

    // Drop from EL2 to EL1 if needed
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
    ldr     x0, =_boot_stack_end
    mov     sp, x0

    // === Init PL011 UART ===
    ldr     x0, =0x9000030
    mov     w1, #0
    str     w1, [x0]
    ldr     x0, =0x9000024
    mov     w1, #27
    str     w1, [x0]
    ldr     x0, =0x9000028
    mov     w1, #8
    str     w1, [x0]
    ldr     x0, =0x900002C
    mov     w1, #0x70
    str     w1, [x0]
    ldr     x0, =0x9000030
    mov     w1, #0x301
    str     w1, [x0]

    // === Identity page tables at 0x40000000 ===
    // L0 at 0x40000000, L1 at 0x40001000, L2 (x4) at 0x40002000-0x40005000
    mov     x9, #0x40000000         // x9 = L0 base

    // Clear L0[0..3]
    str     xzr, [x9]
    str     xzr, [x9, #8]
    str     xzr, [x9, #0x10]
    str     xzr, [x9, #0x18]

    // L1 at 0x40001000
    ldr     x10, =0x40001000
    str     xzr, [x10]
    str     xzr, [x10, #8]
    str     xzr, [x10, #0x10]
    str     xzr, [x10, #0x18]

    // L0[0] = L1
    ldr     x12, =0x40001000
    orr     x12, x12, #3
    str     x12, [x9]

    // L1[0..3] = L2 tables
    ldr     x12, =0x40002000
    orr     x12, x12, #3
    str     x12, [x10]

    ldr     x12, =0x40003000
    orr     x12, x12, #3
    str     x12, [x10, #8]

    ldr     x12, =0x40004000
    orr     x12, x12, #3
    str     x12, [x10, #0x10]

    ldr     x12, =0x40005000
    orr     x12, x12, #3
    str     x12, [x10, #0x18]

    // Fill all 2048 L2 entries with 2MB block descriptors
    // Entries 0-7 (0-16MB): Device (AttrIdx=0)  = 0x421
    // Entries 8-2047 (16MB-4GB): Normal (AttrIdx=1) = 0x725
    ldr     x12, =0x40002000
    mov     x11, #2048
    ldr     x14, =0x421     // Device: Valid|Block|NS=1|AF=1 = 0x421
    ldr     x15, =0x725     // Normal: Valid|Block|NS=1|SH=Inner|AF=1|AttrIdx=1 = 0x725

200:
    sub     x11, x11, #1
    cmp     x11, #8
    csel    x13, x14, x15, lo
    lsl     x0, x11, #21
    orr     x0, x0, x13
    str     x0, [x12, x11, lsl #3]
    cbnz    x11, 200b

    // === Higher half mapping via TTBR1 ===
    // VA for kernel: 0xFFFF800040080000
    //   bits 47:39 = 256 → L0[256]
    //   bits 38:30 = 1   → L1[1]
    //   bits 29:21 = 0   → L2[0] (2MB block, 0x40080000)
    //   bits 29:21 = 1   → L2[1] (2MB block, 0x40280000)
    // L0[256] → L1 at 0x40006000 → L2 at 0x40007000

    // L1 table at 0x40006000 - clear 4 entries
    ldr     x12, =0x40006000
    stp     xzr, xzr, [x12]
    stp     xzr, xzr, [x12, #0x10]

    // L2 table at 0x40007000 - clear all 512 entries
    ldr     x13, =0x40007000
    mov     x14, #512
201:
    sub     x14, x14, #1
    str     xzr, [x13, x14, lsl #3]
    cbnz    x14, 201b

    // L0[256] = L1 table at 0x40006000
    ldr     x12, =0x40006000
    orr     x12, x12, #3      // table descriptor
    str     x12, [x9, #256*8]

    // L1[0] = empty L2 table at 0x40008000 (VA 0xFFFF800000000000, no RAM below 1GB)
    ldr     x14, =0x40008000
    mov     x15, #64
205:
    sub     x15, x15, #1
    str     xzr, [x14, x15, lsl #3]
    cbnz    x15, 205b
    ldr     x12, =0x40008000
    orr     x12, x12, #3
    ldr     x13, =0x40006000  // L1 base
    str     x12, [x13]        // L1[0]

    // L1[1] = L2 table at 0x40007000 (kernel at VA 0xFFFF800040000000)
    ldr     x12, =0x40007000
    orr     x12, x12, #3      // table descriptor
    ldr     x13, =0x40006008  // L1[1]
    str     x12, [x13]

    // L2[0] = 2MB block for kernel text at PA 0x40080000
    ldr     x12, =0x40007000  // L2 base
    ldr     x13, =0x40080000
    ldr     x14, =0x725       // NS|AF|SH=11|Attr=1|Table/Block
    orr     x13, x13, x14
    str     x13, [x12]        // L2[0]

    // L2[1] = 2MB block for kernel data at PA 0x40280000
    ldr     x13, =0x40280000
    orr     x13, x13, x14
    str     x13, [x12, #8]    // L2[1]

    // === Configure MMU ===
    ldr     x8, =0xFF00          // MAIR: Attr0=Device, Attr1=Normal WB WA
    msr     mair_el1, x8

    ldr     x8, =0x535103510     // TCR: T0SZ=16, T1SZ=16, TG0=TG1=4KB, EPD1=0, IPS=48b
    msr     tcr_el1, x8

    msr     ttbr0_el1, x9       // TTBR0 = L0 (identity map)
    msr     ttbr1_el1, x9       // TTBR1 = L0 (higher half via L0[256])

    dsb     sy
    isb

    // Clean page table pages (0x40000000-0x40008000) and invalidate TLBI
    mov     x11, #0x40000000
    mov     x15, #8
202:
    dc      cvac, x11
    add     x11, x11, #0x1000
    subs    x15, x15, #1
    b.ne    202b
    dsb     sy

    tlbi    vmalle1
    dsb     sy
    isb

    // === Enable MMU (M=1, I=1) ===
    mrs     x0, sctlr_el1
    mov     x1, #0x1001
    orr     x0, x0, x1
    dsb     sy
    msr     sctlr_el1, x0
    isb
    dsb     sy
    isb

    // Stay identity-mapped — kernel is linked at PA 0x40080000.
    // higherhalf_entry uses linker VA for data (0x402Fxxxx),
    // which is only reachable via PC-relative adrp from identity-mapped code.
    // Set SP and jump to higherhalf_entry.
    ldr     x0, =_boot_stack_end
    mov     sp, x0

    mov     x0, x20             // DTB pointer saved at _start
    mov     x1, xzr             // mb_info = 0
    bl      higherhalf_entry

    // Should not return
    wfi
    b       .

.section .boot.data, "aw"
.balign 4096
_boot_stack:
    .space  16384
_boot_stack_end:
