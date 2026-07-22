# GDB batch: catch panic / fatal CPU exception during memory class.
set pagination off
set confirm off

target remote :1234

break panic
commands
  silent
  printf "\n=== PANIC: %s ===\n", $arg0
  printf "current=%p id=%d state=%d\n", kernel::Scheduler::current_task(), (kernel::Scheduler::current_task()?kernel::Scheduler::current_task()->id:0u), (kernel::Scheduler::current_task()?(int)kernel::Scheduler::current_task()->state:0)
  printf "save_rsp_to=%p load_rsp_from=0x%lx load_cr3_from=0x%lx next_id=%lu\n", scheduler_save_rsp_to, scheduler_load_rsp_from, scheduler_load_cr3_from, scheduler_next_task_id
  bt 14
  shell echo "PANIC_CAPTURED" > build/gdb-panic-captured
  quit
end

break handle_interrupt_c
commands
  silent
  if $rdi < 0x20
    printf "\n=== CPU EXC vector=%d rip=0x%lx current=%p id=%d state=%d ===\n", $rdi, $rdx, kernel::Scheduler::current_task(), (kernel::Scheduler::current_task()?kernel::Scheduler::current_task()->id:0u), (kernel::Scheduler::current_task()?(int)kernel::Scheduler::current_task()->state:0)
    printf "save_rsp_to=%p load_rsp_from=0x%lx load_cr3_from=0x%lx next_id=%lu\n", scheduler_save_rsp_to, scheduler_load_rsp_from, scheduler_load_cr3_from, scheduler_next_task_id
    bt 10
  end
  continue
end

continue
