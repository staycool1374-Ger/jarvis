# GDB batch script: catch SIGILL (#UD, vector 6) and inspect the faulting task.
set pagination off
set confirm off

target remote :1234

break handle_interrupt_c
commands
  silent
  # handle_interrupt_c(vector=rdi, error_code=rsi, rip=rdx, rsp=rcx)
  if $rdi == 6
    printf "\n=== SIGILL caught (vector=%d) fault_rip=0x%lx error_code=0x%lx ===\n", $rdi, $rdx, $rsi
    set $ct = kernel::Scheduler::current_task()
    printf "current_task() = %p\n", $ct
    if $ct != 0
      printf "  id            = %lu (0x%lx)\n", $ct->id, $ct->id
      printf "  state         = %d (0=READY 1=RUNNING 2=BLOCKED 3=WAITING 4=TERMINATED)\n", (int)$ct->state
      printf "  magic         = 0x%lx\n", $ct->magic
      printf "  page_table_   = 0x%lx\n", $ct->page_table_
      printf "  in_ready_queue_ = %d\n", (int)$ct->in_ready_queue_
      printf "  rq_priority_  = %lu\n", $ct->rq_priority_
      printf "  runq_next_    = %p\n", $ct->runq_next_
      printf "  runq_prev_    = %p\n", $ct->runq_prev_
      printf "  user_stack_   = 0x%lx\n", $ct->user_stack_
      printf "  fault rip     = 0x%lx\n", $rdx
      printf "--- deferred-switch slot state ---\n"
      printf "  scheduler_save_rsp_to    = 0x%lx\n", (unsigned long)scheduler_save_rsp_to
      printf "  scheduler_load_rsp_from  = 0x%lx\n", (unsigned long)scheduler_load_rsp_from
      printf "  scheduler_load_cr3_from  = 0x%lx\n", (unsigned long)scheduler_load_cr3_from
      printf "  isr_nesting_depth        = %ld\n", (long)isr_nesting_depth
      printf "--- backtrace (path that dispatched TERMINATED task) ---\n"
      bt
      printf "--- end SIGILL dump ---\n"
    end
    shell echo "SIGILL_CAPTURED" > build/gdb-panic-captured
    quit
  end
  continue
end

continue
