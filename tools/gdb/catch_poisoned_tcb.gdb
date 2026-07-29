set confirm off
set pagination off
set print pretty on
set remotetimeout 15

source tools/gdb/kernel.py

target extended-remote :1234

break TaskControlBlock::cleanup
commands
  set $tcb = (kernel::TaskControlBlock*)$rdi
  if $tcb->magic != 0x5443424D41474943
    printf "\n========== POISONED TCB in cleanup ==========\n"
    printf "TCB at %p\n", $tcb
    printf "magic=0x%llx id=%lld name=%s\n", $tcb->magic, $tcb->id, $tcb->name
    printf "state=%d priority=%lld page_table_=0x%llx\n", \
      (int)$tcb->state, $tcb->priority, $tcb->page_table_
    printf "\n--- Backtrace ---\n"
    bt 30
    printf "\n--- Current task ---\n"
    task
    printf "\n========================================\n"
    shell touch /tmp/gdb-pf-captured
    quit 1
  end
  continue
end

printf "[GDB] Waiting for poisoned TCB in cleanup()...\n"
continue
