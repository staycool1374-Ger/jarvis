; x86 port I/O assembly functions (nasm-compatible syntax)
; These must be compiled with nasm -f elf64

[bits 64]
[section .text]

global arch_outb
global arch_inb
global arch_outw
global arch_inw
global arch_outl
global arch_inl

; void arch_outb(uint16_t port, uint8_t val)
arch_outb:
    mov dx, di      ; port is in edi (x86_64 ABI: first arg)
    mov al, sil     ; val is in esi (second arg)
    out dx, al
    ret

; uint8_t arch_inb(uint16_t port)
arch_inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

; void arch_outw(uint16_t port, uint16_t val)
arch_outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

; uint16_t arch_inw(uint16_t port)
arch_inw:
    mov dx, di
    xor eax, eax
    in ax, dx
    ret

; void arch_outl(uint16_t port, uint32_t val)
arch_outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

; uint32_t arch_inl(uint16_t port)
arch_inl:
    mov dx, di
    xor eax, eax
    in eax, dx
    ret

global arch_hlt
global arch_pause
global arch_cli
global arch_sti

; void arch_hlt()
arch_hlt:
    hlt
    ret

; void arch_pause()
arch_pause:
    pause
    ret

; void arch_cli()
arch_cli:
    cli
    ret

; void arch_sti()
arch_sti:
    sti
    ret
