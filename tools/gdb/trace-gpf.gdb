set confirm off
set pagination off
set print pretty on
set remotetimeout 10

source tools/gdb/kernel.py

target extended-remote :1234

# Break on GPF handler entry — vector 13 (0xd)
break handle_interrupt_c
commands
  printf "=== handle_interrupt_c entered ===\n"
  printf "vector=%llu error_code=%llu rip=0x%llx\n", $rdi, $rsi, $rdx
  if $rdi == 13
    printf "\n--- GPF DETECTED ---\n"
    info registers
    printf "\n--- Stack (iretq frame) ---\n"
    x/8gx $rsp
    printf "\n--- Tasks ---\n"
    tasks
    printf "\n--- Current task ---\n"
    task 8
    printf "\n--- Backtrace ---\n"
    bt 20
    printf "\n--- Panic captured ---\n"
    shell touch build/gdb-panic-captured
    quit 1
  end
  continue
end

break panic
commands
  printf "\n========== GDB PANIC CAPTURED ==========\n"
  bt 20
  printf "\n--- Registers ---\n"
  info registers
  printf "\n--- Tasks ---\n"
  tasks
  printf "\n========================================\n"
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring for GPF (vector 13) and panic...\n"
continue
