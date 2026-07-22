set pagination off
set confirm off
set height 0
set width 0

target remote :1234

break panic
  commands
  silent
  printf "\n=== PANIC: %s ===\n", $arg0
  bt
  shell echo "PANIC_CAPTURED" > build/gdb-panic-captured
  quit
end

continue
