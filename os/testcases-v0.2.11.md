# Test Cases — v0.2.11 (Phase 2: Shell UX & Utilities)

## Branch: testbed only

## Files To Create

### test_shell_ux.cpp (~20 tests)

#### Shell Builtins
- **shell_help_output**: `help` command prints list of builtins to stdout
- **shell_echo_basic**: `echo hello world` prints "hello world"
- **shell_echo_empty**: `echo` (no args) prints empty line
- **shell_pwd**: `pwd` prints current working directory path
- **shell_clear**: `clear` outputs ANSI escape sequence to reset terminal
- **shell_which_found**: `which echo` prints path of builtin echo
- **shell_which_not_found**: `which nonexistent` prints nothing or error
- **shell_env**: `env` lists all environment variables line by line
- **shell_sleep**: `sleep 1` blocks for approximately 1 second

#### Pipe Chains
- **shell_pipe_basic**: `echo foo | cat` forwards stdout of first command to stdin of second
- **shell_pipe_chain**: `echo foo | cat | cat` chains three commands successfully
- **shell_pipe_broken_sigpipe**: Reader of pipe exits before writer completes -> writer gets SIGPIPE
- **shell_redirect_stdout**: `echo foo > /tmp/out.txt` writes to file
- **shell_redirect_stdin**: `cat < /tmp/out.txt` reads from file

#### Status Bar
- **status_bar_rendered**: Status bar line appears at bottom of framebuffer terminal (inverted text)
- **status_bar_serial_ansi**: Status bar output via serial uses ANSI escape sequences for inversion
- **status_bar_datetime**: Status bar shows current date/time (updates at least every second)
- **status_bar_cpu_memory**: Status bar shows CPU% and memory% usage metrics
- **status_bar_exit_code**: Status bar shows exit code of last command (0 for success, non-zero for failure)

#### Prompt
- **prompt_dynamic_format**: Prompt shows `user@host:/pwd$` format (or configured PROMPT format)
- **prompt_updates_on_chdir**: Prompt path updates after `cd` command
- **prompt_env_variable**: Setting PROMPT env variable changes the prompt format

### test_syscall_dir.cpp (~11 tests)

#### MKDIR
- **syscall_mkdir_basic**: MKDIR creates a new directory at valid path
- **syscall_mkdir_existing**: MKDIR on existing path returns EEXIST
- **syscall_mkdir_on_file**: MKDIR where a file exists at path returns ENOTDIR
- **syscall_mkdir_readonly_fs**: MKDIR on read-only filesystem returns EACCES
- **syscall_mkdir_null_path**: MKDIR with null path returns EFAULT

#### UNLINK
- **syscall_unlink_basic**: UNLINK removes an existing file
- **syscall_unlink_nonexistent**: UNLINK on non-existent path returns ENOENT
- **syscall_unlink_is_directory**: UNLINK on a directory returns EISDIR (use rmdir)
- **syscall_unlink_null_path**: UNLINK with null path returns EFAULT

#### RMDIR
- **syscall_rmdir_basic**: RMDIR removes an empty directory
- **syscall_rmdir_not_empty**: RMDIR on non-empty directory returns ENOTEMPTY
- **syscall_rmdir_not_directory**: RMDIR on a file returns ENOTDIR
- **syscall_rmdir_nonexistent**: RMDIR on non-existent path returns ENOENT
- **syscall_rmdir_null_path**: RMDIR with null path returns EFAULT

### test_initrd_utils.cpp (~7 tests)

#### User-space Utilities
- **initrd_mkdir_binary**: `/bin/mkdir` creates a directory and exits with 0
- **initrd_rm_binary**: `/bin/rm` removes a file and exits with 0
- **initrd_rmdir_binary**: `/bin/rmdir` removes an empty directory and exits with 0
- **initrd_echo_binary**: `/bin/echo` prints its arguments to stdout
- **initrd_pwd_binary**: `/bin/pwd` prints the current working directory
- **initrd_clear_binary**: `/bin/clear` resets the terminal (ANSI escape)
- **initrd_sleep_binary**: `/bin/sleep 1` blocks for approximately 1 second

### IPC Pipeline Robustness
- **pipeline_multiple_redirects**: `cat < in.txt > out.txt` reads from in.txt, writes to out.txt
- **pipeline_parallel_execution**: Commands in pipeline execute concurrently (not sequentially)
- **pipeline_large_data**: 64KB data piped between commands is not corrupted
- **pipeline_stderr_preserved**: Stderr is not captured by pipe (only stdout)
