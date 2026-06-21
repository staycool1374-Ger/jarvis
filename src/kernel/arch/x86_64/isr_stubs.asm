; Jarvis RTOS — Development Roadmap / Kernel Core
; Copyright (C) 2026 Arnold Hasshold
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

extern handle_interrupt_c
extern scheduler_save_rsp_to
extern scheduler_load_rsp_from
extern scheduler_load_cr3_from
extern scheduler_on_context_switch
extern isr_nesting_depth

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
    inc qword [rel isr_nesting_depth]
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
    mov rcx, rsp

    call handle_interrupt_c

    cli
    mov rax, [rel scheduler_save_rsp_to]
    test rax, rax
    jz .restore

    ; Only perform context switch if we are the outermost interrupt (nesting depth == 1).
    ; Nested interrupts (e.g. timer during syscall sti/hlt/cli) must not consume the
    ; context-switch globals set up by the outer interrupt.
    cmp qword [rel isr_nesting_depth], 1
    jne .restore

    mov [rax], rsp
    mov rsp, [rel scheduler_load_rsp_from]
    mov qword [rel scheduler_save_rsp_to], 0

    ; Context switch complete — update current_index_ to the next task
    push rax
    push rcx
    push rdx
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    call scheduler_on_context_switch
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rax

    mov rax, [rel scheduler_load_cr3_from]
    test rax, rax
    jz .restore
    mov cr3, rax
    mov qword [rel scheduler_load_cr3_from], 0

.restore:
    mov qword [rel scheduler_save_rsp_to], 0
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
    dec qword [rel isr_nesting_depth]
    iretq

global __isr_vector
__isr_vector:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep
