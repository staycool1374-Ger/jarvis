; Stack helper: provides 64-bit absolute address of kernel_stack top.
; Avoids R_X86_64_32 relocations that occur when Clang references
; higher-half symbols directly from C++ with -mcmodel=large.
; The dq directive generates R_X86_64_64 which always resolves correctly.

section .rodata
extern kernel_stack
global kernel_stack_top
kernel_stack_top:
    dq kernel_stack + 16384
