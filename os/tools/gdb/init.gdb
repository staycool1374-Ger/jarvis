set confirm off
set pagination off
set print pretty on
set remotetimeout 5

source tools/gdb/kernel.py

define hook-stop
  printf "[GDB] Stopped at %s\n", $rip
  bt 8
end

set backtrace limit 32

document hook-stop
  Auto-print RIP and backtrace on every stop.
end

define step-into
  stepi
end
document step-into
  Single-step one instruction (step into).
end

define step-over
  next
end
document step-over
  Step over function call (next line / instruction).
end

define step-out
  finish
end
document step-out
  Step out of current function (run until return).
end

define trace-syscall
  break kernel::Syscall::handle
  commands
    printf "[SYSCALL] number=%d\n", number
    continue
  end
end
document trace-syscall
  Set up syscall entry tracing (prints syscall number on each call).
end

target extended-remote :1234
