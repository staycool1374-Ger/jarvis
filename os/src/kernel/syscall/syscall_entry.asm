extern syscall_handler

global syscall_entry
syscall_entry:
    swapgs
    mov [gs:0x00], rsp
    mov rsp, [gs:0x08]

    push 0
    push 0xFFFFFFFF80000000
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

    mov rdi, rax
    mov rsi, rbx
    mov rdx, rcx
    mov rcx, rdx
    mov r8, rsi
    mov r9, rdi

    call syscall_handler

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

    mov rsp, [gs:0x00]
    swapgs
    o64 sysret
