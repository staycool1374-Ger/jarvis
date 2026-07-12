set confirm off
set pagination off
set backtrace limit 32
set breakpoint pending on

source tools/gdb/init.gdb

printf "[GDB] connected, setting breakpoint\n"
break test_spinlock.cpp:127
info break
printf "[GDB] continuing to breakpoint...\n"
continue

# When we reach here, b is in scope and state==0 already written.
printf "[GDB] breakpoint hit\n"
set $baddr = (uintptr_t)b
printf "[WATCH] baddr=0x%lx\n", $baddr
watch ((kernel::TaskControlBlock*)$baddr)->state
printf "[WATCH] watchpoint set, continuing...\n"
continue

# Watchpoint hit => print who wrote b->state.
printf "[WATCH] HIT: b->state now %d\n", ((kernel::TaskControlBlock*)$baddr)->state
bt 16
info registers rip rsp rbp
