#ifndef JARVIS_CONFIG_H
#define JARVIS_CONFIG_H

/// @file jarvis_config.h
/// @brief Central compile-time configuration for Jarvis RTOS.
/// All tunables currently scattered as constexpr in 20+ files
/// get a single configuration home.
///
/// Every #define has a Doxygen comment explaining its effect,
/// valid range, and default value. Override any define before
/// including this header to customise the build.

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
#define CONFIG_VERSION "0.3.2-dev"

// ---------------------------------------------------------------------------
// Architecture Detection (set by Makefile ARCH variable)
// ---------------------------------------------------------------------------
// Exactly one of these is defined by the build system:
//   ARCH=x86_64   → -DCONFIG_ARCH_X86_64
//   ARCH=aarch64  → -DCONFIG_ARCH_AARCH64
//   ARCH=riscv64  → -DCONFIG_ARCH_RISCV64

#ifndef CONFIG_ARCH_X86_64
#ifndef CONFIG_ARCH_AARCH64
#ifndef CONFIG_ARCH_RISCV64
// Fallback: detect from compiler defines
#if defined(__x86_64__)
#define CONFIG_ARCH_X86_64 1
#elif defined(__aarch64__)
#define CONFIG_ARCH_AARCH64 1
#elif defined(__riscv64)
#define CONFIG_ARCH_RISCV64 1
#else
#error "Unknown architecture — define CONFIG_ARCH_X86_64, CONFIG_ARCH_AARCH64, or CONFIG_ARCH_RISCV64"
#endif
#endif
#endif
#endif

// ---------------------------------------------------------------------------
// Scheduling Tunables
// ---------------------------------------------------------------------------
/// Maximum number of tasks managed by the scheduler (including idle).
/// Valid range: 2–4096. Default: 64.
#ifndef CONFIG_MAX_TASKS
#define CONFIG_MAX_TASKS 64
#endif

/// Scheduling tick rate in Hz.
/// Valid range: 100–100000. Default: 1000.
/// Must divide the timer clock frequency evenly (PIT: 1193182).
#ifndef CONFIG_TICK_HZ
#define CONFIG_TICK_HZ 1000
#endif

/// Highest schedulable priority (0 = lowest).
/// Valid range: 1–127. Default: 127.
/// Limited to 128 levels for O(1) bitmap scheduler (2 × uint64_t).
#ifndef CONFIG_PRIORITY_CEILING
#define CONFIG_PRIORITY_CEILING 127
#endif

/// Enable preemptive scheduling. Set to 0 for cooperative-only.
/// Default: 1 (enabled).
#ifndef CONFIG_PREEMPTION
#define CONFIG_PREEMPTION 1
#endif

/// Idle task yields to lower power / background hooks.
/// Default: 0 (busy-wait).
#ifndef CONFIG_IDLE_YIELD
#define CONFIG_IDLE_YIELD 0
#endif

/// Enable round-robin time-slicing within the same priority.
/// Default: 1 (enabled).
#ifndef CONFIG_TIME_SLICING
#define CONFIG_TIME_SLICING 1
#endif

/// Maximum number of priority levels for the scheduler bitmap.
/// Default: 128. Must match CONFIG_PRIORITY_CEILING + 1.
#ifndef CONFIG_MAX_PRIORITY
#define CONFIG_MAX_PRIORITY 128
#endif

/// Maximum number of tasks with a SporadicServer attached.
/// Used to bound the O(n) scan in Scheduler::on_tick().
/// Valid range: 0–CONFIG_MAX_TASKS. Default: 8.
#ifndef CONFIG_SPORADIC_SERVER_MAX_TASKS
#define CONFIG_SPORADIC_SERVER_MAX_TASKS 8
#endif

/// Number of ticks per budget unit for SporadicServer consume().
/// Individual servers may override via init() granularity param.
/// Default: 1 (every tick consumes 1 unit).
#ifndef CONFIG_SPORADIC_SERVER_BUDGET_GRANULARITY
#define CONFIG_SPORADIC_SERVER_BUDGET_GRANULARITY 1
#endif

// ---------------------------------------------------------------------------
// Memory Layout Tunables (Architecture-Overridable)
// ---------------------------------------------------------------------------
/// Number of PML4 entries reserved for userspace (0–255).
/// Common to all 48-bit-VA architectures (512-entry page table, split 256/256).
#ifndef CONFIG_PML4_USER_COUNT
#define CONFIG_PML4_USER_COUNT 256
#endif

#if CONFIG_ARCH_X86_64
/// Physical page size in bytes. Must be 4096 on x86_64.
#ifndef CONFIG_PAGE_SIZE
#define CONFIG_PAGE_SIZE 4096
#endif

/// Higher-half direct-map offset for physical memory.
#ifndef CONFIG_HHDM_OFFSET
#define CONFIG_HHDM_OFFSET 0xFFFF800000000000ULL
#endif

/// Maximum user-space virtual address.
#ifndef CONFIG_USER_SPACE_LIMIT
#define CONFIG_USER_SPACE_LIMIT 0x00007FFFFFFFFFFFULL
#endif

#elif CONFIG_ARCH_AARCH64
#ifndef CONFIG_PAGE_SIZE
#define CONFIG_PAGE_SIZE 4096
#endif
#ifndef CONFIG_HHDM_OFFSET
#define CONFIG_HHDM_OFFSET 0xFFFF800000000000ULL
#endif
#ifndef CONFIG_USER_SPACE_LIMIT
#define CONFIG_USER_SPACE_LIMIT 0x0000FFFFFFFFFFFFULL
#endif

#elif CONFIG_ARCH_RISCV64
#ifndef CONFIG_PAGE_SIZE
#define CONFIG_PAGE_SIZE 4096
#endif
#ifndef CONFIG_HHDM_OFFSET
#define CONFIG_HHDM_OFFSET 0xFFFFFFC000000000ULL
#endif
#ifndef CONFIG_USER_SPACE_LIMIT
#define CONFIG_USER_SPACE_LIMIT 0x000000FFFFFFFFFFULL
#endif
#endif

/// Kernel stack size per task in bytes. Must be >= CONFIG_MIN_STACK_SIZE.
/// Default: 65536 (64 KiB).
#ifndef CONFIG_STACK_SIZE
#define CONFIG_STACK_SIZE 65536
#endif

/// Minimum allowed kernel stack size in bytes.
/// Default: 4096.
#ifndef CONFIG_MIN_STACK_SIZE
#define CONFIG_MIN_STACK_SIZE 4096
#endif

/// Total kernel heap size in bytes (used by MemPool and early allocators).
/// Default: 16777216 (16 MiB).
#ifndef CONFIG_HEAP_SIZE
#define CONFIG_HEAP_SIZE 16777216
#endif

// ---------------------------------------------------------------------------
// Subsystem Sizing Constants
// ---------------------------------------------------------------------------
/// Maximum open file descriptors per task.
#ifndef CONFIG_MAX_FDS
#define CONFIG_MAX_FDS 32
#endif

/// Maximum number of mount points.
#ifndef CONFIG_MAX_MOUNTS
#define CONFIG_MAX_MOUNTS 32
#endif

/// Maximum number of registered device drivers.
#ifndef CONFIG_MAX_DRIVERS
#define CONFIG_MAX_DRIVERS 16
#endif

/// Maximum number of daemon tasks (vfsd, iocd, etc).
#ifndef CONFIG_MAX_DAEMONS
#define CONFIG_MAX_DAEMONS 16
#endif

/// Maximum number of programs in the program registry.
#ifndef CONFIG_MAX_PROGRAMS
#define CONFIG_MAX_PROGRAMS 32
#endif

/// Maximum IPC message payload size in bytes.
#ifndef CONFIG_IPC_MAX_MSG_SIZE
#define CONFIG_IPC_MAX_MSG_SIZE 64
#endif

/// Maximum number of messages in an IPC queue.
#ifndef CONFIG_IPC_MAX_QUEUE_MSG
#define CONFIG_IPC_MAX_QUEUE_MSG 16
#endif

/// Number of IPC priority levels.
#ifndef CONFIG_IPC_PRIORITY_LEVELS
#define CONFIG_IPC_PRIORITY_LEVELS 32
#endif

/// Maximum shared memory pages for IPC between user-space and drivers.
#ifndef CONFIG_IPC_SHMEM_MAX_PAGES
#define CONFIG_IPC_SHMEM_MAX_PAGES 64
#endif

/// Maximum physical pages for a user-space process loaded via runelf.
#ifndef CONFIG_MAX_PROCESS_PAGES
#define CONFIG_MAX_PROCESS_PAGES 512
#endif

/// Maximum number of signal handlers per task.
#ifndef CONFIG_MAX_SIGNAL_HANDLERS
#define CONFIG_MAX_SIGNAL_HANDLERS 32
#endif

/// Maximum length of a VFS path string.
#ifndef CONFIG_VFS_MAX_PATH
#define CONFIG_VFS_MAX_PATH 256
#endif

/// Maximum length of a task name (including null terminator).
#ifndef CONFIG_TASK_NAME_LEN
#define CONFIG_TASK_NAME_LEN 16
#endif

/// Maximum number of waiters in a sync primitive (Mutex, Semaphore, Queue, etc).
#ifndef CONFIG_SYNC_MAX_WAITERS
#define CONFIG_SYNC_MAX_WAITERS 32
#endif

/// Kernel log (dmesg) ring buffer capacity in entries. Must be a power of 2.
/// Default: 4096 (~256 KiB at ~64 bytes/entry).
#ifndef CONFIG_DMESG_CAPACITY
#define CONFIG_DMESG_CAPACITY 4096
#endif

// ---------------------------------------------------------------------------
// MemPool Configuration
// ---------------------------------------------------------------------------
/// Number of fixed-size MemPool classes.
#ifndef CONFIG_MEMPOOL_NUM_POOLS
#define CONFIG_MEMPOOL_NUM_POOLS 9
#endif

/// Comma-separated list of block sizes per pool.
/// Must have exactly CONFIG_MEMPOOL_NUM_POOLS entries.
#ifndef CONFIG_MEMPOOL_BLOCK_SIZES
#define CONFIG_MEMPOOL_BLOCK_SIZES 16,32,64,128,256,512,1024,2048,4096
#endif

/// Comma-separated list of block counts per pool.
/// Must have exactly CONFIG_MEMPOOL_NUM_POOLS entries.
#ifndef CONFIG_MEMPOOL_BLOCK_COUNTS
#define CONFIG_MEMPOOL_BLOCK_COUNTS 256,128,64,32,16,8,4,2,1
#endif

// ---------------------------------------------------------------------------
// Syscall Inclusion Gating
// ---------------------------------------------------------------------------
/// Each syscall can be individually enabled/disabled.
/// Set to 0 to strip the syscall at compile time.
#ifndef CONFIG_INCLUDE_SYS_YIELD
#define CONFIG_INCLUDE_SYS_YIELD 1
#endif
#ifndef CONFIG_INCLUDE_SYS_EXIT
#define CONFIG_INCLUDE_SYS_EXIT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_FORK
#define CONFIG_INCLUDE_SYS_FORK 1
#endif
#ifndef CONFIG_INCLUDE_SYS_CLONE
#define CONFIG_INCLUDE_SYS_CLONE 1
#endif
#ifndef CONFIG_INCLUDE_SYS_EXECVE
#define CONFIG_INCLUDE_SYS_EXECVE 1
#endif
#ifndef CONFIG_INCLUDE_SYS_WAITPID
#define CONFIG_INCLUDE_SYS_WAITPID 1
#endif
#ifndef CONFIG_INCLUDE_SYS_NANOSLEEP
#define CONFIG_INCLUDE_SYS_NANOSLEEP 1
#endif
#ifndef CONFIG_INCLUDE_SYS_GETPID
#define CONFIG_INCLUDE_SYS_GETPID 1
#endif
#ifndef CONFIG_INCLUDE_SYS_GETPPID
#define CONFIG_INCLUDE_SYS_GETPPID 1
#endif
#ifndef CONFIG_INCLUDE_SYS_SETPRIO
#define CONFIG_INCLUDE_SYS_SETPRIO 1
#endif
#ifndef CONFIG_INCLUDE_SYS_GETPRIO
#define CONFIG_INCLUDE_SYS_GETPRIO 1
#endif
#ifndef CONFIG_INCLUDE_SYS_SENDSYNC
#define CONFIG_INCLUDE_SYS_SENDSYNC 1
#endif
#ifndef CONFIG_INCLUDE_SYS_RECV
#define CONFIG_INCLUDE_SYS_RECV 1
#endif
#ifndef CONFIG_INCLUDE_SYS_REPLY
#define CONFIG_INCLUDE_SYS_REPLY 1
#endif
#ifndef CONFIG_INCLUDE_SYS_NOTIFY
#define CONFIG_INCLUDE_SYS_NOTIFY 1
#endif
#ifndef CONFIG_INCLUDE_SYS_NOTIFY_WAIT
#define CONFIG_INCLUDE_SYS_NOTIFY_WAIT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_EVENT_POST
#define CONFIG_INCLUDE_SYS_EVENT_POST 1
#endif
#ifndef CONFIG_INCLUDE_SYS_EVENT_WAIT
#define CONFIG_INCLUDE_SYS_EVENT_WAIT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_BRK
#define CONFIG_INCLUDE_SYS_BRK 1
#endif
#ifndef CONFIG_INCLUDE_SYS_MMAP
#define CONFIG_INCLUDE_SYS_MMAP 1
#endif
#ifndef CONFIG_INCLUDE_SYS_MUNMAP
#define CONFIG_INCLUDE_SYS_MUNMAP 1
#endif
#ifndef CONFIG_INCLUDE_SYS_OPEN
#define CONFIG_INCLUDE_SYS_OPEN 1
#endif
#ifndef CONFIG_INCLUDE_SYS_CLOSE
#define CONFIG_INCLUDE_SYS_CLOSE 1
#endif
#ifndef CONFIG_INCLUDE_SYS_READ
#define CONFIG_INCLUDE_SYS_READ 1
#endif
#ifndef CONFIG_INCLUDE_SYS_WRITE
#define CONFIG_INCLUDE_SYS_WRITE 1
#endif
#ifndef CONFIG_INCLUDE_SYS_SEEK
#define CONFIG_INCLUDE_SYS_SEEK 1
#endif
#ifndef CONFIG_INCLUDE_SYS_IOCTL
#define CONFIG_INCLUDE_SYS_IOCTL 1
#endif
#ifndef CONFIG_INCLUDE_SYS_STAT
#define CONFIG_INCLUDE_SYS_STAT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_MKDIR
#define CONFIG_INCLUDE_SYS_MKDIR 1
#endif
#ifndef CONFIG_INCLUDE_SYS_UNLINK
#define CONFIG_INCLUDE_SYS_UNLINK 1
#endif
#ifndef CONFIG_INCLUDE_SYS_RENAME
#define CONFIG_INCLUDE_SYS_RENAME 1
#endif
#ifndef CONFIG_INCLUDE_SYS_MOUNT
#define CONFIG_INCLUDE_SYS_MOUNT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_UMOUNT
#define CONFIG_INCLUDE_SYS_UMOUNT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_REBOOT
#define CONFIG_INCLUDE_SYS_REBOOT 1
#endif
#ifndef CONFIG_INCLUDE_SYS_HALT
#define CONFIG_INCLUDE_SYS_HALT 1
#endif
/// Computed count of enabled syscalls (used for dispatch table sizing).
#define CONFIG_SYSCALL_COUNT \
    (CONFIG_INCLUDE_SYS_YIELD + CONFIG_INCLUDE_SYS_EXIT + \
     CONFIG_INCLUDE_SYS_FORK + CONFIG_INCLUDE_SYS_CLONE + \
     CONFIG_INCLUDE_SYS_EXECVE + CONFIG_INCLUDE_SYS_WAITPID + \
     CONFIG_INCLUDE_SYS_NANOSLEEP + CONFIG_INCLUDE_SYS_GETPID + \
     CONFIG_INCLUDE_SYS_GETPPID + CONFIG_INCLUDE_SYS_SETPRIO + \
     CONFIG_INCLUDE_SYS_GETPRIO + CONFIG_INCLUDE_SYS_SENDSYNC + \
     CONFIG_INCLUDE_SYS_RECV + CONFIG_INCLUDE_SYS_REPLY + \
     CONFIG_INCLUDE_SYS_NOTIFY + CONFIG_INCLUDE_SYS_NOTIFY_WAIT + \
     CONFIG_INCLUDE_SYS_EVENT_POST + CONFIG_INCLUDE_SYS_EVENT_WAIT + \
     CONFIG_INCLUDE_SYS_BRK + CONFIG_INCLUDE_SYS_MMAP + \
     CONFIG_INCLUDE_SYS_MUNMAP + CONFIG_INCLUDE_SYS_OPEN + \
     CONFIG_INCLUDE_SYS_CLOSE + CONFIG_INCLUDE_SYS_READ + \
     CONFIG_INCLUDE_SYS_WRITE + CONFIG_INCLUDE_SYS_SEEK + \
     CONFIG_INCLUDE_SYS_IOCTL + CONFIG_INCLUDE_SYS_STAT + \
     CONFIG_INCLUDE_SYS_MKDIR + CONFIG_INCLUDE_SYS_UNLINK + \
     CONFIG_INCLUDE_SYS_RENAME + CONFIG_INCLUDE_SYS_MOUNT + \
     CONFIG_INCLUDE_SYS_UMOUNT + CONFIG_INCLUDE_SYS_REBOOT + \
     CONFIG_INCLUDE_SYS_HALT)

// ---------------------------------------------------------------------------
// Architecture Feature Detection Flags
// ---------------------------------------------------------------------------
#if CONFIG_ARCH_X86_64
#ifndef CONFIG_HAS_FPU
#define CONFIG_HAS_FPU 1
#endif
#ifndef CONFIG_HAS_RDRAND
#define CONFIG_HAS_RDRAND 1
#endif
#ifndef CONFIG_HAS_APIC
#define CONFIG_HAS_APIC 1
#endif
#endif

#if CONFIG_ARCH_AARCH64
#ifndef CONFIG_HAS_FPU
#define CONFIG_HAS_FPU 1
#endif
#ifndef CONFIG_HAS_GIC
#define CONFIG_HAS_GIC 1
#endif
#endif

#if CONFIG_ARCH_RISCV64
#ifndef CONFIG_HAS_FPU
#define CONFIG_HAS_FPU 1
#endif
#ifndef CONFIG_HAS_PLIC
#define CONFIG_HAS_PLIC 1
#endif
#ifndef CONFIG_HAS_SBI
#define CONFIG_HAS_SBI 1
#endif
#endif

#ifndef CONFIG_HAS_MPU
#define CONFIG_HAS_MPU 0
#endif
#ifndef CONFIG_HAS_HPET
#define CONFIG_HAS_HPET 0
#endif

// ---------------------------------------------------------------------------
// Priority Inheritance Protocol Configuration
// ---------------------------------------------------------------------------
/// Enable Priority Inheritance Protocol for Mutex.
/// When set, a lower-priority task holding a mutex inherits the priority of
/// a higher-priority task waiting on the same mutex. Prevents priority inversion.
/// Set to 0 for soft-RT builds (no PI overhead).
/// Default: 1 (enabled).
#ifndef CONFIG_MUTEX_PIP
#define CONFIG_MUTEX_PIP 0
#endif

/// Enable Priority Inheritance Protocol for Semaphore.
/// When set, if a higher-priority task is blocked waiting on a semaphore and
/// a lower-priority task currently holds/controls it, the lower-priority task
/// inherits the higher priority. Set to 0 for soft-RT builds.
/// Default: 1 (enabled).
#ifndef CONFIG_SEMAPHORE_PIP
#define CONFIG_SEMAPHORE_PIP 1
#endif

/// Enable Priority Inheritance Protocol for Queue (sync::Queue).
/// Boosts the receiver when a high-priority sender is blocked on a full queue,
/// and boosts the sender when a high-priority receiver is blocked on an empty queue.
/// Set to 0 for soft-RT builds.
/// Default: 1 (enabled).
#ifndef CONFIG_QUEUE_PIP
#define CONFIG_QUEUE_PIP 1
#endif

// ---------------------------------------------------------------------------
// Hook Configuration Points
// ---------------------------------------------------------------------------
/// If non-zero, declares weak symbol idle_hook() called each idle iteration.
#ifndef CONFIG_IDLE_HOOK
#define CONFIG_IDLE_HOOK 0
#endif

/// If non-zero, declares weak symbol tick_hook(uint64_t ticks) called on tick.
#ifndef CONFIG_TICK_HOOK
#define CONFIG_TICK_HOOK 0
#endif

/// If non-zero, declares weak symbol stack_overflow_hook(TaskControlBlock*).
#ifndef CONFIG_STACK_OVERFLOW_HOOK
#define CONFIG_STACK_OVERFLOW_HOOK 0
#endif

/// If non-zero, declares weak symbol oom_hook(size_t requested_size).
#ifndef CONFIG_OOM_HOOK
#define CONFIG_OOM_HOOK 0
#endif

/// If non-zero, declares weak symbol init_hook() after daemon init complete.
#ifndef CONFIG_INIT_HOOK
#define CONFIG_INIT_HOOK 0
#endif

/// If non-zero, declares weak symbol sporadic_server_deadline_handler(...).
#ifndef CONFIG_SPORADIC_SERVER_DEADLINE_HOOK
#define CONFIG_SPORADIC_SERVER_DEADLINE_HOOK 1
#endif

/// Enable deadline miss detection in on_tick(). Default: 1 (enabled).
#ifndef CONFIG_DEADLINE_MISS_DETECTION
#define CONFIG_DEADLINE_MISS_DETECTION 1
#endif

/// Enable WCET overrun detection in on_tick(). Default: 1 (enabled).
/// When enabled, tasks with wcet_ticks > 0 are monitored for execution-
/// time overrun (executed_ticks > wcet_ticks).
#ifndef CONFIG_WCET_OVERRUN_DETECTION
#define CONFIG_WCET_OVERRUN_DETECTION 0
#endif

/// When set, SporadicServer budget exhaustion (consume()->budget→0) also
/// sets task->deadline_missed = true, mapping exhaustion to a deadline miss.
#ifndef CONFIG_SPORADIC_SERVER_EXHAUSTION_IS_DEADLINE
#define CONFIG_SPORADIC_SERVER_EXHAUSTION_IS_DEADLINE 0
#endif

/// Decouple deadline scanning from the timer ISR via a dedicated watchdog task.
/// When >0, scheduler spawns [deadline-mon] at priority 127 during init().
/// The monitor waits on an atomic flag (lock-free handoff) and calls
/// scan_deadlines() in task context where full scheduler_lock_ and inline
/// KILL cleanup are safe.  When 0, inline scanning in on_tick() is used.
/// Default: 0 (inline).
#ifndef CONFIG_DEADLINE_MONITOR_TASK
#define CONFIG_DEADLINE_MONITOR_TASK 1
#endif

/// Default action on deadline miss (used by default handler):
///   0 = LOG_ONLY, 1 = PANIC, 2 = DEMOTE (priority /= 2), 3 = KILL (deferred
///       cleanup via remove_task + cleanup + free), 4 = NOTIFY_MONITOR (send
///       SIGUSR1 to CONFIG_DEADLINE_MONITOR_PID)
#ifndef CONFIG_DEADLINE_ACTION
#define CONFIG_DEADLINE_ACTION 0
#endif

/// PID of the monitor task to notify when CONFIG_DEADLINE_ACTION == 4.
/// Set to 0 to disable notification delivery at compile time.
#ifndef CONFIG_DEADLINE_MONITOR_PID
#define CONFIG_DEADLINE_MONITOR_PID 0
#endif

// ---------------------------------------------------------------------------
// Custom Assertion Macro
// ---------------------------------------------------------------------------
/// Overridable assertion macro. Default calls panic on failure.
#ifndef CONFIG_ASSERT
extern "C" void panic(const char* msg);
#define CONFIG_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            panic("CONFIG_ASSERT: " #expr " at " __FILE__); \
        } \
    } while (0)
#endif

#endif // JARVIS_CONFIG_H
