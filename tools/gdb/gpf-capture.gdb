set confirm off
set pagination off
set remotetimeout 60
set logging file build/gdb-capture.log
set logging on

target extended-remote :1234

break panic
commands
  printf "\n========== PANIC CAPTURED ==========\n"

  printf "scheduler_save_rsp_to   = 0x%llx\n", scheduler_save_rsp_to
  printf "scheduler_load_rsp_from = 0x%llx\n", scheduler_load_rsp_from
  printf "scheduler_load_cr3_from = 0x%llx\n", scheduler_load_cr3_from
  printf "scheduler_next_task_id  = 0x%llx\n", scheduler_next_task_id

  printf "task_count_    = %llu\n", kernel::Scheduler::task_count_
  printf "current_index_ = %llu\n", kernel::Scheduler::current_index_

  printf "tasks_[0] = 0x%llx\n", kernel::Scheduler::tasks_[0]
  printf "tasks_[1] = 0x%llx\n", kernel::Scheduler::tasks_[1]
  printf "tasks_[2] = 0x%llx\n", kernel::Scheduler::tasks_[2]
  printf "tasks_[3] = 0x%llx\n", kernel::Scheduler::tasks_[3]
  printf "tasks_[4] = 0x%llx\n", kernel::Scheduler::tasks_[4]

  printf "\nDone.\n"
  shell touch build/gdb-panic-captured
  quit 1
end

continue
