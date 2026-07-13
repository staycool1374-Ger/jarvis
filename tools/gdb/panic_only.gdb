set pagination off
set confirm off
set height 0
set width 0
target remote :1234
break kernel.cpp:1161
commands
  printf "PANIC faulting_rip=0x%llx cr2=0x%llx handler_rsp=0x%llx\n", $rip, $cr2, $rsp
  info registers rip rsp rbp cr2 rax rbx rcx rdx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
  printf "=== handler stack (rsp-0x100..rsp+0x40) ===\n"
  x/48gx $rsp - 0x100
  bt
  quit
end
continue
