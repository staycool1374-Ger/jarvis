set confirm off
set pagination off
set print pretty on
set remotetimeout 60

source tools/gdb/kernel.py

target extended-remote :1234

# Catch genuine FAULT vectors only (not #NM=7 which is the normal lazy-FPU
# path). 14=#PF, 13=#GP, 8=double-fault, 0=divide, 6=invalid-op, 12=stack.
# The silent IPC-dispatch hang is a kernel-mode fault in the timer ISR's
# dispatch path; one of these vectors fires before the PIT EOI, freezing ticks.
break handle_interrupt_c if ($rdi == 14 || $rdi == 13 || $rdi == 8 || $rdi == 6 || $rdi == 12 || $rdi == 0)
commands
  printf "\n========== FAULT (vector %llu) ==========\n", $rdi
  printf "error_code=0x%llx\n", $rsi
  printf "cr2(fault addr)=0x%llx\n", $cr2
  printf "\n--- Registers at handle_interrupt_c entry ---\n"
  info registers
  printf "\n--- Backtrace ---\n"
  bt 24
  printf "\n--- Scheduler dispatch globals ---\n"
  printf "scheduler_save_rsp_to   = 0x%llx\n", scheduler_save_rsp_to
  printf "scheduler_load_rsp_from = 0x%llx\n", scheduler_load_rsp_from
  printf "scheduler_load_cr3_from = 0x%llx\n", scheduler_load_cr3_from
  printf "scheduler_next_task_id  = 0x%llx\n", scheduler_next_task_id
  printf "isr_nesting_depth       = 0x%llx\n", isr_nesting_depth
  printf "current_task_ptr_       = 0x%llx\n", kernel::Scheduler::current_task_ptr_
  printf "\n--- Tasks ---\n"
  tasks
  printf "\n========================================\n"
  shell touch build/gdb-panic-captured
  quit 1
end

# Also catch explicit panics (kernel-mode fault that reached panic()).
break panic
commands
  printf "\n========== PANIC ==========\n"
  bt 24
  info registers
  tasks
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring for fault vectors (14/13/8/6/12/0) during IPC dispatch...\n"
continue
