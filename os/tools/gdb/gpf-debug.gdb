# GPF debug script with context switch tracing
set pagination off
set confirm off

echo === Connecting to QEMU GDB stub ===\n
target remote :1234

echo \n=== Setting breakpoints ===\n

# Break at scheduler_on_context_switch to see every context switch
break *scheduler_on_context_switch
commands
  silent
  printf "=== CONTEXT SWITCH ===\n"
  printf "scheduler_next_task_id (rdi) = 0x%llx (%llu)\n", $rdi, $rdi
  printf "Current RSP=0x%llx\n", $rsp
  # Dump scheduler state
  printf "scheduler_save_rsp_to=0x%llx\n", *(unsigned long long*)&scheduler_save_rsp_to
  printf "scheduler_load_rsp_from=0x%llx\n", *(unsigned long long*)&scheduler_load_rsp_from
  # Dump registers about to be popped
  printf "Popa area (15 GPRs) at RSP:\n"
  x/15gx $rsp
  printf "IRETQ frame at RSP+120:\n"
  x/5gx $rsp + 120
  continue
end

# Break at handle_interrupt_c for vector 13 (GPF)
break *handle_interrupt_c if $rdi == 13
commands
  silent
  printf "\n\n=== GPF CAPTURED ===\n"
  printf "error_code=0x%llx  rip=0x%llx\n", $rsi, $rdx

  # Scheduler globals
  printf "\nScheduler globals:\n"
  printf "  scheduler_save_rsp_to=0x%llx\n", *(unsigned long long*)&scheduler_save_rsp_to
  printf "  scheduler_load_rsp_from=0x%llx\n", *(unsigned long long*)&scheduler_load_rsp_from
  printf "  scheduler_load_cr3_from=0x%llx\n", *(unsigned long long*)&scheduler_load_cr3_from
  printf "  scheduler_next_task_id=0x%llx\n", *(unsigned long long*)&scheduler_next_task_id

  # regs = 4th arg in rcx
  set $r = $rcx
  printf "\nSaved registers (regs at 0x%llx):\n", $r
  printf "RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n", *(long long*)($r), *(long long*)($r+8), *(long long*)($r+16), *(long long*)($r+24)
  printf "RSI=0x%llx RDI=0x%llx RBP=0x%llx R8=0x%llx\n", *(long long*)($r+32), *(long long*)($r+40), *(long long*)($r+48), *(long long*)($r+56)
  printf "R9=0x%llx  R10=0x%llx R11=0x%llx R12=0x%llx\n", *(long long*)($r+64), *(long long*)($r+72), *(long long*)($r+80), *(long long*)($r+88)
  printf "R13=0x%llx R14=0x%llx R15=0x%llx\n", *(long long*)($r+96), *(long long*)($r+104), *(long long*)($r+112)

  # CPU exception frame at regs+120 (vector, error_code, RIP, CS, RFLAGS, RSP, SS)
  printf "\nCPU exception frame (regs+120):\n"
  printf "  vector=0x%llx err=0x%llx RIP=0x%llx\n", *(long long*)($r+120), *(long long*)($r+128), *(long long*)($r+136)
  printf "  CS=0x%llx RFLAGS=0x%llx RSP=0x%llx SS=0x%llx\n", *(long long*)($r+144), *(long long*)($r+152), *(long long*)($r+160), *(long long*)($r+168)
  
  # Dump full stack frame from regs
  printf "\nFull stack from regs:\n"
  x/25gx $r

  # Task table
  printf "\n=== TASK TABLE ===\n"
  set $tasks = (unsigned long long)&_ZN6kernel9Scheduler6tasks_E
  set $task_count = *(unsigned long long*)&_ZN6kernel9Scheduler11task_count_E
  set $current_idx = *(unsigned long long*)&_ZN6kernel9Scheduler14current_index_E
  printf "task_count=%llu current_index=%llu\n", $task_count, $current_idx

  set $i = 0
  while $i < $task_count
    set $t = *(unsigned long long*)($tasks + $i*8)
    if $t != 0
      printf "  [%llu] tcb=0x%llx id=%llu state=%llu\n", $i, $t, *(unsigned long long*)$t, *(unsigned long long*)($t+8)
    else
      printf "  [%llu] NULL\n", $i
    end
    set $i = $i + 1
  end

  # Now we want to see the popa area / iretq frame that would have been used
  # in the timer ISR that caused the GPF
  printf "\n=== TIMER ISR REGISTER SAVE (speculative) ===\n"
  # The load_rsp_from would be the RSP loaded by the context-switch ISR
  printf "scheduler_load_rsp_from=0x%llx\n", *(unsigned long long*)&scheduler_load_rsp_from
  set $load_rsp = *(unsigned long long*)&scheduler_load_rsp_from
  if $load_rsp != 0
    printf "Popa area at scheduler_load_rsp_from:\n"
    x/15gx $load_rsp
    printf "IRETQ frame at scheduler_load_rsp_from+120:\n"
    x/5gx $load_rsp + 120
  end

  # Dump what gpf_rsp points to (the stack at exception)
  printf "\n=== STACK AT GPF EXCEPTION (gpf_rsp) ===\n"
  printf "gpf_rsp = rsp value from CPU exception frame = 0x%llx\n", *(long long*)($r+160)
  set $gpf_rsp_val = *(long long*)($r+160)
  if $gpf_rsp_val != 0
    printf "Stack around gpf_rsp:\n"
    x/16gx $gpf_rsp_val - 64
  end

  quit
end

echo \n=== Running... ===\n
continue

echo \n=== Target exited ===\n
quit
