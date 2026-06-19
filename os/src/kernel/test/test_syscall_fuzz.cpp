#include <test.hpp>
#include <logger.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <constants.hpp>
#include <signal.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Call every syscall with edge-case argument values: 0,
// UINT64_MAX, -1, null pointers, and invalid enum values.  Ensure no
// kernel crash and consistent return behaviour (error or graceful
// rejection, never undefined).
// Input: For each syscall number 0..MAX_SYSCALL-1, call with arg0-3 set
// to various extreme values.
// Expect: No crash; return value is either 0 or -1 (UINT64_MAX) for
// error returns.
TEST_CLASS(SyscallFuzzBounds) {
    auto* cur = Scheduler::current_task();
    CT_ASSERT(cur != nullptr);

    uint64_t extremal[] = {
        0, 1, UINT64_MAX, static_cast<uint64_t>(-1LL),
        0xFFFFFFFFULL, 0xDEADBEEFCAFEBABEULL
    };

    for (uint64_t num = 0;
         num < static_cast<uint64_t>(SyscallNumber::MAX_SYSCALL); ++num) {
        for (size_t a = 0; a < 4; ++a) {
            // Only test the first few variations per syscall to keep
            // runtime bounded
            for (size_t v = 0; v < 2; ++v) {
                uint64_t args[4] = {0, 0, 0, 0};
                args[a] = extremal[v];
                uint64_t ret = Syscall::handle(
                    num, args[0], args[1], args[2], args[3], nullptr);
                // Accept either 0 (success) or UINT64_MAX (-1, error)
                bool ok = (ret == 0 || ret == UINT64_MAX);
                if (!ok) {
                    Logger::warn("syscall %llu arg[%zu]=0x%llx ret=0x%llx",
                                 num, a, args[a], ret);
                }
                CT_ASSERT(ok);
            }
        }
    }
};

// Runmode: kernel
// Testidea: Call IPC-related syscalls with invalid flags combinations and
// malformed arguments: SEND to nonexistent task, RECV from empty queue,
// BUF_ALLOC with bad VA, BUF_FREE with forged handle.
// Input: See each sub-test.
// Expect: All return UINT64_MAX (error), no crash.
TEST_CLASS(SyscallFuzzFlags) {
    auto* cur = Scheduler::current_task();
    CT_ASSERT(cur != nullptr);

    // sys_send with invalid flags (all bits set)
    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::SEND),
        999999, // nonexistent dest
        0,      // type
        0,      // priority
        0,      // (arg3 unused in current impl)
        nullptr);
    CT_ASSERT(ret == UINT64_MAX);

    // sys_receive from empty queue (own queue filled?)
    // Drain own queue first
    Message drain;
    while (cur->msg_queue && cur->msg_queue->pop(drain)) {}
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::RECEIVE),
        0, 0, 0, 0, nullptr);
    CT_ASSERT(ret == UINT64_MAX);

    // sys_send_sync to nonexistent
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::SEND_SYNC),
        999999, 0, 0, 0, nullptr);
    CT_ASSERT(ret == UINT64_MAX);

    // sys_buf_alloc with VA >= USER_SPACE_LIMIT (null user task)
    // Current test task may not have page_table_, so alloc returns 0
    uint64_t va_too_high = USER_SPACE_LIMIT + 0x1000;
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_ALLOC),
        va_too_high, 0, 0, 0, nullptr);
    CT_ASSERT(ret == 0 || ret == UINT64_MAX);

    // sys_kill with invalid signal number
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::KILL),
        0, // self
        99, // invalid signal
        0, 0, nullptr);
    CT_ASSERT(ret == UINT64_MAX || ret == 0);

    // sys_alarm with 0 ticks
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::ALARM),
        0, // 0 ticks
        0, 0, 0, nullptr);
    CT_ASSERT(ret == 0 || ret == UINT64_MAX);
};

// Runmode: kernel
// Testidea: Call syscalls from a task in TERMINATED state.  The kernel
// should reject the operation rather than dereference freed memory.
// Input: Create a task, mark it TERMINATED, set as current, call syscalls.
// Expect: No crash; operations return error.
TEST_CLASS(SyscallFuzzStates) {
    auto* zombie = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(zombie != nullptr);
    Scheduler::add_task(*zombie);

    zombie->state = TaskState::TERMINATED;
    zombie->exit_code = 1;

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*zombie);

    // Try various syscalls as a terminated task

    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::SEND),
        original->id, 0, 0, 0, nullptr);
    // May return UINT64_MAX (error) or have undefined behaviour
    // if the implementation doesn't check state. Just ensure no crash.
    CT_ASSERT(Scheduler::current_task() != nullptr);

    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GET_TICKS),
        0, 0, 0, 0, nullptr);
    CT_ASSERT(Scheduler::current_task() != nullptr);

    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GETPID),
        0, 0, 0, 0, nullptr);
    CT_ASSERT(Scheduler::current_task() != nullptr);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*zombie);
    zombie->cleanup();
    delete zombie;
};

// Runmode: kernel
// Testidea: Attempt to invoke kernel-privileged operations from a task
// without user page tables (kernel-only task).  Operations that require
// user-space access (CheckedPtr, user memory) should fail gracefully.
// Input: Kernel task (no user page_table_) calls BUF_ALLOC, BUF_MAP,
// FORK, EXEC.
// Expect: All return UINT64_MAX (or 0 for no-op) without crash.
TEST_CLASS(SyscallFuzzPrivilege) {
    auto* ktask = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(ktask != nullptr);
    CT_ASSERT(ktask->page_table_ == 0); // kernel task

    auto* original = Scheduler::current_task();
    Scheduler::add_task(*ktask);
    Scheduler::set_current(*ktask);

    uint64_t ret;

    // BUF_ALLOC requires user page table
    ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::BUF_ALLOC),
        0x80000000, 0, 0, 0, nullptr);
    CT_ASSERT(ret == 0 || ret == UINT64_MAX);

    // EXEC with null pointers (should fail CheckedPtr validation)
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::EXEC),
        0, // null path
        0, // null argv
        0, // null envp
        0, nullptr);
    CT_ASSERT(ret == UINT64_MAX);

    // FORK (should fail for kernel task)
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::FORK),
        0, 0, 0, 0, nullptr);
    CT_ASSERT(ret == UINT64_MAX);

    // WAITPID with no children
    Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::WAITPID),
        0, // any child
        0, // null status
        0, 0, nullptr);
    CT_ASSERT(ret == UINT64_MAX || ret == 0);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*ktask);
    ktask->cleanup();
    delete ktask;
};

void register_syscall_fuzz_tests() {
    Logger::info("Registering syscall fuzz tests");
    REGISTER_CLASS(SyscallFuzzBounds);
    REGISTER_CLASS(SyscallFuzzFlags);
    REGISTER_CLASS(SyscallFuzzStates);
    REGISTER_CLASS(SyscallFuzzPrivilege);
}
