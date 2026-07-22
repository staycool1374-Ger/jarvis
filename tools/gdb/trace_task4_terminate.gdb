# GDB batch script: find WHERE task 4 transitions to TERMINATED.
# Breaks at every kernel-side `state = TERMINATED` site (and Scheduler::terminate),
# conditioned on the affected task's id == 4, and dumps the backtrace at the
# exact transition. Also keeps the SIGILL catch as a safety net.
set pagination off
set confirm off

target remote :1234

# Primary terminate path
break Scheduler::terminate
commands
  silent
  if task.id == 4
    printf "\n>>> Scheduler::terminate called on task 4 (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end terminate(task4) backtrace\n"
  end
  continue
end

# Raw TERMINATED assignments (kernel side only)
break src/kernel/task/scheduler.cpp:1087
commands
  silent
  if t->id == 4
    printf "\n>>> scheduler.cpp:1087 cleanup_test_tasks set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

break src/kernel/task/scheduler.cpp:1996
commands
  silent
  if task->id == 4
    printf "\n>>> scheduler.cpp:1996 set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

break src/kernel/syscall/syscall_handlers_misc.cpp:93
commands
  silent
  if t->id == 4
    printf "\n>>> syscall_handlers_misc.cpp:93 set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

break src/kernel/syscall/syscall_handlers_process.cpp:180
commands
  silent
  if t->id == 4
    printf "\n>>> syscall_handlers_process.cpp:180 set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

break src/kernel/task/taskdefs.cpp:189
commands
  silent
  if t->id == 4
    printf "\n>>> taskdefs.cpp:189 set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

break src/kernel/daemon/daemon_mgr.cpp:334
commands
  silent
  if task->id == 4
    printf "\n>>> daemon_mgr.cpp:334 set task4 TERMINATED (current=%p)\n", kernel::Scheduler::current_task()
    bt
    printf ">>> end backtrace\n"
  end
  continue
end

# Safety net: catch the SIGILL too
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
