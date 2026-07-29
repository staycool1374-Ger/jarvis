# VULN-004: Catch ENSURE failure in map_page_in_pml4
# Print backtrace, phys_addr, and va to identify the caller

set pagination off
set confirm off

# Break at the ENSURE line (x86_64 path, ~line 517)
# The ENSURE calls panic() which calls handle_interrupt_c
# Instead, break right before the leaf PTE write in map_page_in_pml4

break *map_page_in_pml4 + 0x100 if $rdi == 1
# Actually, let's just break at the panic handler
break kernel::panic_if

commands
  bt 20
  info locals
  print "VULN-004: map_page_in_pml4 ENSURE failed"
  # Print the function arguments from the calling frame
  frame 1
  print "map_page_in_pml4 args:"
  print/x $rdi  # virt_addr (x86_64 calling convention: rdi, rsi, rdx, rcx, r8, r9)
  print/x $rsi  # phys_addr
  print/x $rdx  # user
  print/x $rcx  # pml4_phys
  continue
end

continue
