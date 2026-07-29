set confirm off
set pagination off
set print pretty on
set remotetimeout 15

source tools/gdb/kernel.py

target extended-remote :1234

# First, find the HHDM PDPT physical address once booted
printf "[GDB] Waiting for boot...\n"
break kernel::VMM::init
commands
  printf "VMM::init entered — will find PDPT address\n"
  continue
end

break kernel::VMM::init
commands
  printf "\n=== VMM::init done ===\n"
  set $kpml4 = kernel::VMM::kernel_pml4_
  set $pml4 = (unsigned long long*)(0xFFFF800000000000 + ($kpml4 & ~0xfffULL))
  set $pdpt_phys = $pml4[256] & ~0xfffULL
  printf "kernel_pml4_ = 0x%llx\n", $kpml4
  printf "HHDM PDPT phys = 0x%llx\n", $pdpt_phys
  printf "PDPT[63] at virt 0x%llx\n", 0xFFFF800000000000 + $pdpt_phys + 63*8
  # Set hardware watchpoint on PDPT[63]
  awatch *(unsigned long long*)(0xFFFF800000000000 + $pdpt_phys + 63*8)
  commands
    printf "\n========== PDPT[63] MODIFIED ==========\n"
    printf "New value = 0x%llx\n", *(unsigned long long*)(0xFFFF800000000000 + $pdpt_phys + 63*8)
    printf "\n--- Current task ---\n"
    task
    printf "\n--- Backtrace ---\n"
    bt 30
    printf "\n--- All regs ---\n"
    info registers
    printf "\n========================================\n"
    shell touch /tmp/gdb-pf-captured
    quit 1
  end
  continue
end

printf "[GDB] Monitoring PDPT[63]...\n"
continue
