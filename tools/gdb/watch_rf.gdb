set pagination off
set confirm off
target remote :1234

break kernel::test::run_filtered
commands
  silent
  stepi
  stepi
  stepi
  watch *($rbp + 8)
  continue
end

continue
