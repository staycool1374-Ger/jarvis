# GDB batch script: verify sys_exit safe switch-away fix (no SIGILL, slot set).
set pagination off
set confirm off

target remote :1234

# Break right after sys_exit sets the task TERMINATED (the raw assignment).
break src/kernel/syscall/syscall_handlers_misc.cpp:94
commands
  silent
  printf "\n>>> sys_exit: task id=%d set TERMINATED (current=%p)\n", t->id, kernel::Scheduler::current_task()
  bt 6
  continue
end

# Break inside the new safe switch-away helper and dump the published slot.
break kernel::Scheduler::switch_away_from_terminating
commands
  silent
  printf "\n>>> switch_away_from_terminating(exiting id=%d)\n", exiting.id
  printf "    scheduler_save_rsp_to before = %p\n", kernel::scheduler_save_rsp_to
  printf "    current_task = %p\n", kernel::Scheduler::current_task()
  bt 6
  continue
end

# After the helper returns, confirm the slot was published (non-null).
break src/kernel/syscall/syscall_handlers_misc.cpp:156
commands
  silent
  printf "\n>>> back in sys_exit after switch-away; save_rsp_to=%p load_rsp_from=0x%lx\n", kernel::scheduler_save_rsp_to, kernel::scheduler_load_rsp_from
  if kernel::scheduler_save_rsp_to == 0
    printf "!!! BUG: deferred switch NOT published after sys_exit\n"
    shell echo "SWITCH_NOT_PUBLISHED" > build/gdb-panic-captured
  else
    printf "OK: deferred switch published\n"
    shell echo "SWITCH_PUBLISHED" > build/gdb-sigill-ok
  end
  continue
end

# Safety net: catch SIGILL.
break handle_interrupt_c
commands
  silent
  if $rdi == 6
    printf "\n=== SIGILL (vector=6) fault_rip=0x%lx; current=%p state=%d ===\n", $rdx, kernel::Scheduler::current_task(), (int)kernel::Scheduler::current_task()->state
    shell echo "SIGILL_CAPTURED" > build/gdb-panic-captured
    quit
  end
  continue
end

continue
