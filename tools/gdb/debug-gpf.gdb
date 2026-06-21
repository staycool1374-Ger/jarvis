set confirm off
set pagination off
set print pretty on
set remotetimeout 10

source tools/gdb/kernel.py

target extended-remote :1234

# --- Break on GPF (vector 13) ---
break handle_interrupt_c
commands
  if $rdi == 13
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
    printf "\n--- Task 8 detail ---\n"
    task 8
    printf "\n--- Backtrace ---\n"
    bt 20
    printf "\n========================================\n"
    shell touch build/gdb-panic-captured
    quit 1
  end
  continue
end

# --- Break on daemon reload ---
break kernel::test::reload_daemon_tasks
commands
  printf "\n=== reload_daemon_tasks entered ===\n"
  printf "Tasks:\n"
  tasks
  printf "scheduler_next_task_id=0x%llx\n", scheduler_next_task_id
  continue
end

# --- Break on panic ---
break panic
commands
  printf "\n========== PANIC ==========\n"
  bt 20
  info registers
  tasks
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring release boot for GPF...\n"
continue
