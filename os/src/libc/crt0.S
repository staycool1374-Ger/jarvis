.section .text
.global _start
_start:
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    lea 16(%rsp,%rdi,8), %rdx
    call main
    mov %rax, %rdi
    call exit
    hlt
