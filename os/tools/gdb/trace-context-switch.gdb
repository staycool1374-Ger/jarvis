# Trace context switches and catch the GPF
set pagination off
set confirm off

echo === TARGET DISPLAY MODE ===\n
target remote :1234

echo \n=== Setting breakpoints ===\n

# Break at scheduler_on_context_switch to see every context switch
break *scheduler_on_context_switch
commands
  silent
  printf "=== CONTEXT SWITCH ===\n"
  printf "On CPU: \n"
  printf "RSP=0x%llx  CR3=0x%llx\n", $rsp, $cr3
  # scheduler_next_task_id is in rdi (already passed as argument)
  printf "Switching to task_id = 0x%llx (%llu)\n", $rdi, $rdi
  # Dump the scheduler state
  printf "scheduler_save_rsp_to=0x%llx\n", *(unsigned long long*)&scheduler_save_rsp_to
  printf "scheduler_load_rsp_from=0x%llx\n", *(unsigned long long*)&scheduler_load_rsp_from
  printf "scheduler_load_cr3_from=0x%llx\n", *(unsigned long long*)&scheduler_load_cr3_from
  printf "scheduler_next_task_id=0x%llx\n", *(unsigned long long*)&scheduler_next_task_id
  # Dump the registers on the stack (the "new" task's saved context)
  printf "Popa area on new task's stack (15 regs from RSP=0x%llx):\n", $rsp
  x/16gx $rsp
  # After popa, iretq frame at RSP+120 (15 regs)
  printf "IRETQ frame at RSP+120 (RIP, CS, RFLAGS, RSP, SS):\n"
  x/5gx $rsp + 120
  continue
end

# Break at GPF handler to see the crash site
break *handle_interrupt_c
commands
  silent
  printf "\n\n=== INTERRUPT/EXCEPTION ===\n"
  printf "Vector=0x%llx Error=0x%llx RIP=0x%llx\n", $rdi, $rsi, $rdx
  if $rdi == 13
    printf "\n=== GPF CAPTURE ===\n"
    printf "scheduler_save_rsp_to=0x%llx\n", *(unsigned long long*)&scheduler_save_rsp_to
    printf "scheduler_load_rsp_from=0x%llx\n", *(unsigned long long*)&scheduler_load_rsp_from
    printf "scheduler_load_cr3_from=0x%llx\n", *(unsigned long long*)&scheduler_load_cr3_from
    printf "scheduler_next_task_id=0x%llx\n", *(unsigned long long*)&scheduler_next_task_id
    printf "rsp=0x%llx\n", $rsp
    printf "cr3=0x%llx\n", $cr3
    printf "Saved registers:\n"
    # regs is in rcx (4th argument)
    # regs[0..14] = rax..r15
    printf "rax=0x%llx rbx=0x%llx rcx=0x%llx rdx=0x%llx\n", *(unsigned long long*)($rcx), *(unsigned long long*)($rcx+8), *(unsigned long long*)($rcx+16), *(unsigned long long*)($rcx+24)
    printf "rsi=0x%llx rdi=0x%llx rbp=0x%llx r8=0x%llx\n", *(unsigned long long*)($rcx+32), *(unsigned long long*)($rcx+40), *(unsigned long long*)($rcx+48), *(unsigned long long*)($rcx+56)
    printf "r9=0x%llx  r10=0x%llx r11=0x%llx r12=0x%llx\n", *(unsigned long long*)($rcx+64), *(unsigned long long*)($rcx+72), *(unsigned long long*)($rcx+80), *(unsigned long long*)($rcx+88)
    printf "r13=0x%llx r14=0x%llx r15=0x%llx\n", *(unsigned long long*)($rcx+96), *(unsigned long long*)($rcx+104), *(unsigned long long*)($rcx+112)
    # Dump the CPU frame
    printf "\nCPU exception frame (error_code, RIP, CS, RFLAGS, RSP, SS):\n"
    x/6gx $rcx + 120
    printf "\nStack dump around regs:\n"
    x/32gx $rsp
    printf "\n=== BACKTRACE ===\n"
    bt 10
    # Dump task table summary
    printf "\n=== TASK TABLE ===\n"
    set $tasks = (unsigned long long)0xffff8000004a9f30
    set $task_count = *(unsigned long long*)0xffff8000004aa530
    set $current_idx = *(unsigned long long*)0xffff8000004aa538
    printf "task_count=%llu current_index=%llu\n", $task_count, $current_idx
    set $i = 0
    while $i < $task_count
      set $task = *(unsigned long long*)($tasks + $i*8)
      if $task != 0
        printf "  [%llu] tcb=0x%llx id=%llu state=%llu prio=%llu\n", $i, $task, *(unsigned long long*)$task, *(unsigned long long*)($task+8), *(unsigned long long*)($task+0x18)
      end
      set $i = $i + 1
    end
    quit
  end
  continue
end

echo \n=== Starting execution (continue) ===\n
continue
