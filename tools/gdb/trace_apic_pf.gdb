set confirm off
set pagination off
set print pretty on
set remotetimeout 15

source tools/gdb/kernel.py

target extended-remote :1234

# --- Break on Page Fault in arch::lapic_wr ---
break handle_interrupt_c
commands
  if $rdi == 14
    set $err = $rsi
    set $rip = $rdx
    printf "\n========== PF at rip=0x%llx err=0x%llx ==========\n", $rip, $err
    set $kpml4 = kernel::VMM::kernel_pml4_
    printf "CR3=0x%llx  kernel_pml4_=0x%llx\n", $cr3, $kpml4
    printf "\n--- HHDM page table walk ---\n"
    set $pml4 = (unsigned long long*)(0xFFFF800000000000 + ($kpml4 & ~0xfffULL))
    printf "PML4[256] = 0x%llx\n", $pml4[256]
    set $pdpt = $pml4[256] & ~0xfffULL
    if $pdpt != 0
      set $pdpt_v = (unsigned long long*)(0xFFFF800000000000 + $pdpt)
      printf "PDPT[63]  = 0x%llx\n", $pdpt_v[63]
      set $pd = $pdpt_v[63] & ~0xfffULL
      if $pd != 0
        set $pd_v = (unsigned long long*)(0xFFFF800000000000 + $pd)
        printf "APIC PD   = 0x%llx\n", $pd
        printf "PD[0x7F0] = 0x%llx\n", $pd_v[0x7F0]
        set $pt = $pd_v[0x7F0] & ~0xfffULL
        if $pt != 0
          set $pt_v = (unsigned long long*)(0xFFFF800000000000 + $pt)
          printf "APIC PT   = 0x%llx\n", $pt
          printf "PT[0]     = 0x%llx (mapped)\n", $pt_v[0]
        else
          printf "APIC PT NOT PRESENT (dangling PD entry!)\n"
        end
      else
        printf "APIC PD NOT PRESENT\n"
      end
    else
      printf "PDPT[63] NOT PRESENT\n"
    end

    printf "\n--- PMM state ---\n"
    printf "free_pages_=%lld  free_head_=%lld  pool_free_head_=%lld\n", \
      kernel::PMM::free_pages_, kernel::PMM::free_head_, kernel::PMM::pool_free_head_

    printf "\n--- Current task ---\n"
    task

    printf "\n--- Backtrace ---\n"
    bt 20
    printf "\n========================================\n"
    shell touch /tmp/gdb-pf-captured
    quit 1
  end
  continue
end

printf "[GDB] Monitoring for APIC Page Fault in all-1...\n"
continue
