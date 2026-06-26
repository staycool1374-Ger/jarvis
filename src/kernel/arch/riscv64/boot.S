.section .text.boot
.global _start
_start:
    /* OpenSBI starts us in S-mode with:
     *   a0 = hart ID
     *   a1 = DTB pointer
     *   satp = 0 (Bare)
     *   We are identity-mapped at the physical load address.
     */

    /* Save boot info */
    mv s0, a0
    mv s1, a1

    /* Only hart 0 proceeds; park others */
    li t0, 0
    beq a0, t0, 1f
0:  wfi
    j 0b
1:

    /* Set up a temporary stack (physical address, identity-mapped) */
    la sp, _boot_stack_top

    /* Simple Sv39 boot page tables */

    /* L0[256] -> L1 table (kernel region VPN[2]=256) */
    la t0, _boot_pt_l0
    la t1, _boot_pt_l1
    li t2, 0x1F               /* V | R | W | X */
    or t1, t1, t2
    li t3, 2048               /* 256 * 8 */
    add t3, t0, t3
    sd t1, 0(t3)              /* L0[256] = L1 table desc */

    /* Also identity-map L0[0] for low 1GB */
    la t1, _boot_pt_l1_id
    or t1, t1, t2
    sd t1, 0(t0)              /* L0[0] = L1_id table desc */

    /* L1[1] -> L2 table (kernel VPN[1]=1) */
    la t0, _boot_pt_l1
    la t1, _boot_pt_l2
    or t1, t1, t2
    li t3, 8                   /* 1 * 8 */
    add t3, t0, t3
    sd t1, 0(t3)              /* L1[1] = L2 table desc */

    /* L1_id[0] -> L2_id table (identity VPN[1]=0) */
    la t0, _boot_pt_l1_id
    la t1, _boot_pt_l2_id
    or t1, t1, t2
    sd t1, 0(t0)              /* L1_id[0] = L2_id table desc */

    /* L2[32]: 2MB block at 0x80200000 (kernel text/data VPN[0]=32) */
    la t0, _boot_pt_l2
    li t1, 0x80200
    slli t1, t1, 10           /* PPN field at bits 53:10 */
    li t2, 0xFF               /* V|R|W|X|U|G|A|D */
    or t1, t1, t2
    li t3, 256                /* 32 * 8 */
    add t3, t0, t3
    sd t1, 0(t3)              /* L2[32] = 0x80200000 2MB block */

    /* L1_id[1] -> L2_id (identity VPN[1]=1, for VA 0x00200000-0x003FFFFF) */
    la t0, _boot_pt_l1_id
    la t1, _boot_pt_l2_id
    or t1, t1, t2
    li t3, 8                   /* 1 * 8 */
    add t3, t0, t3
    sd t1, 0(t3)              /* L1_id[1] = L2_id table desc */

    /* L2_id[0]: 2MB block at 0x80000000 (identity, covers qemu RAM start) */
    la t0, _boot_pt_l2_id
    li t1, 0x80000
    slli t1, t1, 10
    li t2, 0xFF
    or t1, t1, t2
    sd t1, 0(t0)              /* L2_id[0] = 0x80000000 2MB block */

    /* L2_id[32]: 2MB block at 0x80200000 (identity, covers boot code) */
    la t0, _boot_pt_l2_id
    li t1, 0x80200
    slli t1, t1, 10
    li t2, 0xFF
    or t1, t1, t2
    li t3, 256                /* 32 * 8 */
    add t3, t0, t3
    sd t1, 0(t3)              /* L2_id[32] = 0x80200000 2MB block */

    /* Enable MMU: SATP = (Sv39 << 60) | PPN(L0) */
    la t0, _boot_pt_l0
    srli t0, t0, 12
    li t1, 0x8000000000000000 /* Sv39 = 8, shifted to bits 63:60 */
    or t0, t1, t0
    csrw satp, t0
    sfence.vma

    /* Now running with higher-half mapping active.
     * Stack still at identity address — switch to higher-half.
     */
    la sp, _boot_stack_top
    li t0, 0xFFFFFFC000000000
    add sp, sp, t0

    /* Jump to higherhalf_entry in higher-half (literal pool load for 64-bit addr) */
    la t0, .L_higherhalf_ptr
    ld t0, 0(t0)
    mv a0, s0                /* hart ID in a0 */
    mv a1, s1                /* DTB pointer in a1 */
    jalr ra, t0

    /* Should not return. If we do, SBI shutdown. */
    li a7, 8
    ecall

.align 3
.L_higherhalf_ptr:
    .quad higherhalf_entry

.section .boot.bss, "aw", @nobits
.align 12
_boot_pt_l0:
    .space 4096
_boot_pt_l1:
    .space 4096
_boot_pt_l2:
    .space 4096
_boot_pt_l1_id:
    .space 4096
_boot_pt_l2_id:
    .space 4096
_boot_stack:
    .space 4096
_boot_stack_top:
