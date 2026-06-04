extern handle_interrupt_c
extern scheduler_save_rsp_to
extern scheduler_load_rsp_from

%macro ISR_NOERR 1
global isr_%1
isr_%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr_%1
isr_%1:
    push %1
    jmp isr_common
%endmacro

ISR_NOERR  0
ISR_NOERR  1
ISR_NOERR  2
ISR_NOERR  3
ISR_NOERR  4
ISR_NOERR  5
ISR_NOERR  6
ISR_NOERR  7
ISR_ERR    8
ISR_NOERR  9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

%assign i 32
%rep 224
ISR_NOERR i
%assign i i+1
%endrep

section .text
isr_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, [rsp + 15*8]
    mov rsi, [rsp + 16*8]
    mov rdx, [rsp + 17*8]

    call handle_interrupt_c

    mov rax, [rel scheduler_save_rsp_to]
    test rax, rax
    jz .restore

    mov [rax], rsp
    mov rsp, [rel scheduler_load_rsp_from]
    mov qword [rel scheduler_save_rsp_to], 0

.restore:
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

global __isr_vector
__isr_vector:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep
