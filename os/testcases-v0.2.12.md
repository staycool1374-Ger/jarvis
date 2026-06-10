# Test Cases — v0.2.12 (Phase 3: System Services)

## Branch: testbed only

*Outline — test details to be expanded when implementation begins.*

### tmpfs (Temporary Filesystem) — test_tmpfs.cpp
- Basic mount/unmount lifecycle
- File create, read, write, close round-trip
- Directory creation and listing
- Hard link quota enforcement
- User-specific quota limits
- OOM protection: allocation rejected at quota boundary
- Unlinking frees pages
- Concurrent read/write from multiple tasks

### Init System (PID 1) — test_init.cpp
- `/sbin/init` boots as PID 1 in userspace
- Service lifecycle: spawn, monitor, restart on crash
- `/etc/rc` script parsing and sequential execution
- Orphaned children reparented to init
- SIGCHLD handling from terminated children

### fstab — test_fstab.cpp
- fstab format parsing (device, mountpoint, fstype, options)
- Auto-mount all entries at boot
- Invalid fstab entries are skipped gracefully
- Missing device in fstab reports error
- Duplicate mountpoint detection

### Resource Limits & Heap — test_rlimit.cpp
- `SYS_GETRLIMIT` / `SYS_SETRLIMIT` on max_fds, max_stack, max_memory
- Exceeding `max_fds` returns EMFILE
- Exceeding `max_stack` triggers segfault
- `SYS_BRK` expands heap (positive increment)
- `SYS_BRK` contracts heap (negative increment)
- `SYS_BRK` with invalid address returns -1
- brk/sbrk wrapper in libc matches kernel behavior

### Core Text Utilities — test_textutils.cpp
- `/bin/less` paginates stdin, handles up/down arrows
- `/bin/ed` opens file, accepts line-oriented commands
- Signal handling (SIGINT, SIGTERM) during pager session
