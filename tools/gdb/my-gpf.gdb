set confirm off
set pagination off
set remotetimeout 60
set logging file build/gdb-capture.log
set logging on

target extended-remote :1234

# Track kernel-stack allocations for the runner.
break kernel::TaskControlBlock::create
commands
  silent
  # stack is allocated inside create; we can't see it easily, so just log entry
  continue
end

# Catch any free of a kernel stack page. The runner's stack is at phys 0x16b4000
# (vir 0xffff8000016b4000). Flag frees of that exact page.
break kernel::PMM::free_page
commands
  silent
  printf "PMMFREE page=0x%lx cur=%p(id=%u)\n", (unsigned long long)$arg0, \
    (void*)kernel::Scheduler::current_task_ptr_, (kernel::Scheduler::current_task_ptr_?kernel::Scheduler::current_task_ptr_->id:0U)
  continue
end

# Also report stack frees inside cleanup()
break kernel::TaskControlBlock::cleanup
commands
  silent
  printf "CLEANUP id=%u name='%s' stack_phys=0x%lx\n", this->id, this->name, (unsigned long long)this->stack_phys_
  continue
end

break kernel.cpp:1161
commands
  printf "\n========== FATAL CPU EXCEPTION PATH ==========\n"
  bt
  info registers rip rsp rbp rdi rsi rdx rcx rbx rax
  shell touch build/gdb-panic-captured
  quit 1
end

continue
