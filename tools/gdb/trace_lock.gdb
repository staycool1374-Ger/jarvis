set confirm off
set pagination off
set remotetimeout 5
target extended-remote :1234

# Watch scheduler_lock_.locked_ (static member)
# Find its address from the ELF
shell x86_64-elf-nm build/kernel-debug.elf | grep scheduler_lock_ | head -3

# Set a watchpoint on locked_ (it's at offset 0 within SpinLock, which is the first member)
# We need to stop ONLY when locked_ transitions to 1 and stays there.
# Instead, let's set a breakpoint at the TICK trace when lk=0
# and check the backtrace.

# Break at on_tick's TICK probe where lock is not held
break *Scheduler::on_tick+0x13c if scheduler_lock_.locked_ != 0

# Break at the exact point where TICK trace prints lk=0
# Look at the print: if (lk_held) is false
# We can break at the ipc_sched_trace call

# Actually, let's use a simpler approach: break on scheduler_lock_.lock()
# and scheduler_lock_.unlock() and log the call stack

# Set logging
set logging file /tmp/gdb-trace.log
set logging on

# Break on lock and unlock operations
break kernel::sync::SpinLock::lock
commands
  printf "[LOCK] acquire at %p from %p\n", $rip, __builtin_return_address(0)
  bt 5
  continue
end

break kernel::sync::SpinLock::unlock
commands
  printf "[UNLOCK] release at %p from %p\n", $rip, __builtin_return_address(0)
  continue
end

# Also break on try_lock to see who acquires
break kernel::sync::SpinLock::try_lock
commands
  printf "[TRY_LOCK] at %p from %p\n", $rip, __builtin_return_address(0)
  continue
end

# Continue running
continue
