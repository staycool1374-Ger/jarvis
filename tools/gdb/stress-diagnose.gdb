set confirm off
set pagination off
target extended-remote :1234

# Break at the stress test function
break spinlock_multi_task_contention
commands
  printf "[GDB] stress test started\n"
  break stress_worker
  commands
    printf "[GDB] stress_worker started id=%d\n", ((kernel::TaskControlBlock*)((char*)$rdi - 8))->id
    continue
  end
  continue
end

# Break on context switch to watch task transitions
break scheduler_on_context_switch
commands
  silent
  continue
end

# Catch any kernel panic
break panic
commands
  printf "[GDB] KERNEL PANIC at %s\n", $rip
  bt
  info registers
  continue
end

# Run until the test loop
continue

# When we stop at spinlock_multi_task_contention, advance past create/add_task
# and into the loop, then inspect state
tbreak spinlock_multi_task_contention
commands
  printf "[GDB] At spinlock_multi_task_contention entry\n"
  # Single step to the reschedule/hlt loop
  # Look for the loop by advancing past the worker creation
  advance *spinlock_multi_task_contention+0x80
  printf "[GDB] After worker creation, at loop start\n"
  # Dump all tasks
  set $tc = kernel::Scheduler::task_count()
  set $i = 0
  while $i < $tc
    set $t = kernel::Scheduler::task_at($i)
    if $t != 0
      printf "  task[%d] id=%d prio=%d state=%d rsp=0x%llx kstack=0x%llx\n", $i, $t->id, $t->priority, $t->state, $t->context.rsp, $t->kernel_stack
    end
    set $i = $i + 1
  end
  # Dump scheduler globals
  printf "save_rsp_to=0x%llx load_rsp_from=0x%llx nesting=%d\n", kernel::scheduler_save_rsp_to, kernel::scheduler_load_rsp_from, kernel::isr_nesting_depth
  continue
end

continue
