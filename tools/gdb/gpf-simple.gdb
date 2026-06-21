set confirm off
set pagination off
set print pretty on
set remotetimeout 10

target extended-remote :1234

break panic
commands
  printf "\n========== PANIC CAPTURED ==========\n"
  bt 20
  info registers
  printf "\nScheduler globals:\n"
  printf "scheduler_save_rsp_to=0x%llx\n", scheduler_save_rsp_to
  printf "scheduler_load_rsp_from=0x%llx\n", scheduler_load_rsp_from
  printf "scheduler_load_cr3_from=0x%llx\n", scheduler_load_cr3_from
  printf "scheduler_next_task_id=0x%llx\n", scheduler_next_task_id
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring for panic...\n"
continue
