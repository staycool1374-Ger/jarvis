set pagination off
set confirm off
file build/kernel-debug.elf

# Break at process_replenishments to catch the -1 this pointer
b *SporadicServer::process_replenishments(unsigned long long)+0x1c
commands
  silent
  printf "process_replenishments called with this=0x%llx\n", $rdi
  if $rdi == 0xFFFFFFFFFFFFFFFF
    printf "CORRUPT sporadic_server detected! Backtrace:\n"
    bt
    printf "Scheduler tasks:\n"
    # Walk the task list
    set $tc = *(unsigned long long*)0xFFFF8000005338C0
    printf "task_count_ = %llu\n", $tc
    quit
  end
  continue
end

# Alternative: break at the crash point (page fault handler)
b *0xFFFF800000204D40
commands
  silent
  printf "In fault handler\n"
  bt
  info registers
  continue
end

run
