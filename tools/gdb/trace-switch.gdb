set confirm off
set pagination off
set print pretty on
set remotetimeout 60
set breakpoint pending on
source tools/gdb/kernel.py
target extended-remote :1234

set logging file build/gdb-trace.txt
set logging enabled on

tbreak init_task_main
  commands
    continue
  end

break *0xffff80000021559b
  commands
    printf "[CHK] lookup_in_dir al=%d\n", $al
    continue
  end
break *0xffff8000002155c3
  commands
    printf "[CHK] alloc_cluster al=%d\n", $al
    continue
  end
break *0xffff80000021570a
  commands
    printf "[CHK] write_cluster al=%d\n", $al
    continue
  end
break *0xffff800000215799
  commands
    printf "[CHK] add_dir_entry al=%d\n", $al
    continue
  end
break *0xffff8000002157d7
  commands
    printf "[FAIL] dir_data alloc failed\n"
    continue
  end
break *0xffff8000002157f8
  commands
    printf "[FAIL] write_cluster failed\n"
    continue
  end
break *0xffff800000215828
  commands
    printf "[FAIL] add_dir_entry failed\n"
    continue
  end
break *0xffff8000002157a1
  commands
    printf "[OK] success path\n"
    continue
  end

printf "[GDB] armed. Continue; run 'cd /mnt' then double 'mkdir test'.\n"
continue
