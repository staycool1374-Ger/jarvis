# GDB batch: BUGS.md#020 panic capture + full TCB-reference dump.
# Reliable: `break panic` + `continue` works in this env (catch_panic.gdb proven).
set pagination off
set confirm off
set height 0
set width 0

target remote :1234

break panic
commands
  silent
  printf "\n========== PANIC CAPTURED ==========\n"
  printf "msg: %s\n", $arg0
  set $ct = kernel::Scheduler::current_task()
  printf "current=%p id=%d magic=0x%lx state=%d\n", $ct, ($ct?(uint64_t)$ct->id:0u), ($ct?(uint64_t)$ct->magic:0u), ($ct?(int)$ct->state:0)
  printf "--- scanning all tasks for poisoned / dangling TCB refs ---\n"
  set $n = 0
  while $n < 64
    set $t = kernel::Scheduler::task_at($n)
    if $t != 0
      set $ss = (uint64_t)$t->sporadic_server
      set $fc = (uint64_t)$t->first_child
      set $ns = (uint64_t)$t->next_sibling
      set $par = (uint64_t)$t->parent_id
      printf "  id=%d magic=0x%lx st=%d ss=%p fc=%p ns=%p par=%lu\n", \
             $t->id, (uint64_t)$t->magic, (int)$t->state, \
             $t->sporadic_server, $t->first_child, $t->next_sibling, $t->parent_id
      if $ss == 0xDDDDDDDDDDDDDDDDULL || $fc == 0xDDDDDDDDDDDDDDDDULL || $ns == 0xDDDDDDDDDDDDDDDDULL
        printf "    >>> POISONED REF in id=%d (UAF!)\n", $t->id
      end
    end
    set $n = $n + 1
  end
  printf "--- end task scan ---\n"
  bt 16
  shell echo "PANIC_CAPTURED" > build/gdb-panic-captured
  quit
end

continue
