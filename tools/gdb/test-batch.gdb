set confirm off
set pagination off
set print pretty on
set remotetimeout 10

source tools/gdb/kernel.py

target extended-remote :1234

break panic
commands
  printf "\n========== GDB PANIC CAPTURED ==========\n"
  printf "Message: %s\n", (const char*)msg
  printf "========================================\n"
  bt 16
  printf "\n--- Registers ---\n"
  info registers
  printf "\n--- Task State ---\n"
  tasks
  printf "\n========================================\n"
  shell touch build/gdb-panic-captured
  quit 1
end

printf "[GDB] Monitoring for panic...\n"
continue
