set pagination off
set confirm off
target remote :1234

break kernel::test::run_filtered
commands
  silent
  stepi
  stepi
  stepi
  echo RUN_FILTERED rbp=
  print/x $rbp
  continue
end

break kernel::test::run_one
commands
  silent
  stepi
  stepi
  stepi
  echo RUN_ONE rbp=
  print/x $rbp
  quit
end

continue
