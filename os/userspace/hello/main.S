.section .text
.global _start
_start:
    mov $99, %rax
    int $0x80
    mov $6, %rax
    int $0x80
    jmp _start
