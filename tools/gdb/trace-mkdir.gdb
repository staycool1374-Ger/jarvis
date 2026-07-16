set confirm off
set pagination off
set print pretty on
set remotetimeout 60
source tools/gdb/kernel.py
target extended-remote :1234

break Terminal::write
commands
  printf "\n>>> Terminal::write(str=%s)\n", $rdi
  bt 24
  continue
end

break kernel::vfs::resolve
commands
  printf "  [vfs] resolve(%s)\n", $rdi
  continue
end

break scheduler_diag_pre_save
commands
  printf "\n!!! DIAG pre-save: live rsp=0x%llx cur=%p\n", $rsp, kernel::Scheduler::current_task_ptr_
  continue
end

printf "[GDB] tracing Terminal::write. Send 'mkdir /tmp/t1'.\n"
continue
