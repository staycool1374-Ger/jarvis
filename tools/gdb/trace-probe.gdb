set confirm off
set pagination off
set remotetimeout 30
source tools/gdb/kernel.py
target extended-remote :1234
set logging file build/gdb-probe.txt
set logging redirect on
set logging on
printf "[PROBE] current_task()=%p\n", (void*)kernel::Scheduler::current_task()
printf "[PROBE] current_task_ptr_=%p\n", (void*)kernel::Scheduler::current_task_ptr_
printf "[PROBE] &current_task_ptr_=%p\n", (void*)&kernel::Scheduler::current_task_ptr_
printf "[PROBE] switch_to_task sym=%p\n", (void*)&switch_to_task
printf "[PROBE] scheduler_diag_pre_save sym=%p\n", (void*)&scheduler_diag_pre_save
quit
