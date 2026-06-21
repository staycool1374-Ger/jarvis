section .multiboot_header

MAGIC          equ 0xE85250D6
ARCH           equ 0

multiboot_start:
    dd MAGIC
    dd ARCH
    dd multiboot_end - multiboot_start
    dd 0x100000000 - (MAGIC + ARCH + (multiboot_end - multiboot_start))

    dw 5       ; tag type: framebuffer request
    dw 1       ; flags: optional
    dd 20      ; size
    dd 1024    ; width
    dd 768     ; height
    dd 32      ; depth

    align 8, db 0

    dw 0       ; end tag type
    dw 0       ; end tag flags
    dd 8       ; end tag size
multiboot_end:
