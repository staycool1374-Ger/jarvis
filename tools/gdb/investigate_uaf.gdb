# GDB batch: investigate BUGS.md#020 (0xDD use-after-free).
# At the fatal panic, dump current task and scan EVERY task's kernel stack for
# MemPool poison (0xDDDDDDDDDDDDDDDD) to find the freed/in-reuse stack, plus
# dump all tasks' magic/state/stack/context.rsp so we can see which TCB is
# poisoned or dangling.
set pagination off
set confirm off

target remote :1234

break panic
commands
  silent
  printf "\n=== PANIC: %s ===\n", $arg0
  printf "current=%p id=%d state=%d magic=0x%lx\n", kernel::Scheduler::current_task(), (kernel::Scheduler::current_task()?kernel::Scheduler::current_task()->id:0u), (kernel::Scheduler::current_task()?(int)kernel::Scheduler::current_task()->state:0), (kernel::Scheduler::current_task()?(uint64_t)kernel::Scheduler::current_task()->magic:0u)
  set $ct = kernel::Scheduler::current_task()
  if $ct != 0
    printf "  context.rsp=0x%lx kernel_stack=%p top=%p\n", $ct->context.rsp, $ct->kernel_stack, $ct->kernel_stack_top
  end

  # Scan every live task's kernel stack for 0xDDDDDDDDDDDDDDDD poison.
  printf "--- scanning all tasks' stacks for 0xDDDDDDDDDDDDDDDD ---\n"
  set $t = kernel::Scheduler::current_task()
  set $n = 0
  while $n < 64
    set $tk = kernel::Scheduler::task_at($n)
    if $tk != 0 && $tk->magic == 0x5443424D41474943ULL
      set $base = (uint64_t)$tk->kernel_stack
      set $top = (uint64_t)$tk->kernel_stack_top
      if $base != 0 && $top != 0
        set $poison = 0
        set $i = $base
        while $i < $top
          if *((uint64_t*)$i) == 0xDDDDDDDDDDDDDDDD
            set $poison = $poison + 1
          end
          set $i = $i + 8
        end
        if $poison > 0
          printf "  TASK id=%d state=%d stack=%p..%p POISON qwords=%lu\n", $tk->id, (int)$tk->state, $tk->kernel_stack, $tk->kernel_stack_top, $poison
        end
      end
    end
    set $n = $n + 1
  end
  printf "--- end stack scan ---\n"
  printf "=== end panic dump ===\n"
  shell echo "PANIC_CAPTURED" > build/gdb-panic-captured
  quit
end

continue
