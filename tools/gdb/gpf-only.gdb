set confirm off
set pagination off
set print pretty on
set remotetimeout 10

source tools/gdb/kernel.py

target extended-remote :1234

# --- Break on GPF only (vector 13) —— do NOT break on every interrupt
break handle_interrupt_c if $rdi == 13
commands
  printf "\n========== GPF (vector 13) DETECTED ==========\n"
  printf "error_code=0x%llx fault_rip=0x%llx\n", $rsi, $rdx
  printf "\n--- All GPRs ---\n"
  info registers
  printf "\n--- Stack (20 qwords) ---\n"
  x/20gx $rsp
  printf "\n--- Scheduler globals ---\n"
  printf "scheduler_save_rsp_to=0x%llx\n", scheduler_save_rsp_to
  printf "scheduler_load_rsp_from=0x%llx\n", scheduler_load_rsp_from
  printf "scheduler_load_cr3_from=0x%llx\n", scheduler_load_cr3_from
  printf "scheduler_next_task_id=0x%llx\n", scheduler_next_task_id
  printf "\n--- Tasks ---\n"
  tasks
  printf "\n--- Detail of task at current_index ---\n"
  task 8
  printf "\n--- Backtrace ---\n"
  bt 20
  printf "\n========================================\n"
  shell touch build/gdb-panic-captured
  quit 1
end

# --- Break on daemon reload ---
break kernel::test::reload_daemon_tasks
commands
  printf "\n=== reload_daemon_tasks entered ===\n"
  printf "Tasks before:\n"
  tasks
  printf "scheduler_next_task_id=0x%llx\n", scheduler_next_task_id
  continue
end

# --- Break on panic ---
break panic
commands
  printf "\n========== PANIC =====\n"
  bt 20
  info registers
  tasks
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring for exceptions...\n"
continue
