section .boot.text
bits 32

%define PAGE_PRESENT_WRITE (1 << 0 | 1 << 1)
%define PAGE_HUGE (1 << 7)

%define PML4_BASE     0x1000
%define PDPT_IDENTITY 0x2000
%define PD_IDENTITY   0x3000
%define PDPT_HIGHER   0x4000
%define PD_HIGHER     0x5000

global _start
extern higherhalf_entry
_start:
    mov [multiboot_magic_local], eax
    mov [multiboot_info_ptr_local], ebx

    cli
    cld

    call init_page_tables

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt64_ptr]

    jmp 0x08:.L64

bits 64
.L64:
    mov edi, [multiboot_magic_local]
    mov esi, [multiboot_info_ptr_local]
    mov rax, higherhalf_entry
    jmp rax

bits 32
init_page_tables:
    mov edi, PML4_BASE
    mov cr3, edi

    ; PML4[0] → PDPT_IDENTITY (identity map)
    mov dword [edi], PDPT_IDENTITY | PAGE_PRESENT_WRITE
    mov dword [edi + 4], 0

    ; PML4[256] → PDPT_HIGHER (higher half)
    mov dword [edi + 256*8], PDPT_HIGHER | PAGE_PRESENT_WRITE
    mov dword [edi + 256*8 + 4], 0

    ; PDPT_IDENTITY[0] → PD_IDENTITY
    mov edi, PDPT_IDENTITY
    mov dword [edi], PD_IDENTITY | PAGE_PRESENT_WRITE
    mov dword [edi + 4], 0

    ; PDPT_HIGHER[0] → PD_HIGHER
    mov edi, PDPT_HIGHER
    mov dword [edi], PD_HIGHER | PAGE_PRESENT_WRITE
    mov dword [edi + 4], 0

    ; PD_IDENTITY: identity-map first 128 MiB
    mov edi, PD_IDENTITY
    xor ecx, ecx
    mov eax, 0 | (PAGE_PRESENT_WRITE | PAGE_HUGE)
    xor edx, edx
.Lloop_ident:
    mov [edi + ecx * 8], eax
    mov [edi + ecx * 8 + 4], edx
    add eax, 0x200000
    adc edx, 0
    inc ecx
    cmp ecx, 64
    jb .Lloop_ident

    ; PD_HIGHER: map higher half to physical 0, PD[1]=0x200000 for kernel
    mov edi, PD_HIGHER
    xor ecx, ecx
    mov eax, 0 | (PAGE_PRESENT_WRITE | PAGE_HUGE)
    xor edx, edx
.Lloop_higher:
    mov [edi + ecx * 8], eax
    mov [edi + ecx * 8 + 4], edx
    add eax, 0x200000
    adc edx, 0
    inc ecx
    cmp ecx, 64
    jb .Lloop_higher

    ret

section .boot.rodata

gdt64:
    dq 0x0000000000000000
    dq 0x00209A0000000000
    dq 0x0000920000000000

gdt64_ptr:
    dw gdt64_ptr - gdt64 - 1
    dq gdt64

section .boot.bss

multiboot_magic_local:    resd 1
multiboot_info_ptr_local:  resd 1
