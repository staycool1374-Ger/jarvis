# Trace the GPF in map_page_in_pml4
# Break at the crash point (testb $0x80,(%r14) = checking PAGE_HUGE)
# And at the function entry to capture arguments

set pagination off
set confirm off

# Break at function entry (x86_64 path)
break *VMM::map_page_in_pml4
commands
  silent
  printf "map_page_in_pml4(va=0x%lx, phys=0x%lx, user=%d, pml4_phys=0x%lx)\n", $rdi, $rsi, $rdx, $rcx
  # Check if phys is beyond HHDM (128MB)
  if $rsi > 0x8000000
    printf "*** PHYS BEYOND HHDM WINDOW! phys=0x%lx\n", $rsi
  end
  continue
end

# Break at the GPF dumper in handle_interrupt_c
break kernel::handle_interrupt_c
commands
  # Check for vector 13 (GPF) or 14 (Page Fault)
  if $rdi == 13 || $rdi == 14
    printf "=== CPU EXCEPTION vector=%lld ===\n", $rdi
    printf "RIP=0x%lx  err=0x%lx\n", $rdx, $rsi
    bt 20
    info registers
    # Check if the faulting address is a small value (null deref)
    if $rdi == 14
      printf "*** CR2=0x%lx\n", $cr2
    end
    printf "=== END DUMP ===\n"
  end
  continue
end

continue
