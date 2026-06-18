#include <test.hpp>
#include <logger.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

struct Rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

enum RlimitResource {
    RLIMIT_DATA = 0,
    RLIMIT_STACK = 1,
    RLIMIT_NOFILE = 2,
};

JARVIS_TEST(sys_getrlimit_nofile) {
    Rlimit rl;
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETRLIMIT),
        RLIMIT_NOFILE, reinterpret_cast<uint64_t>(&rl), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(rl.rlim_cur > 0);
    JARVIS_ASSERT(rl.rlim_max > 0);
    JARVIS_ASSERT_EQ(rl.rlim_cur, rl.rlim_max);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sys_getrlimit_stack) {
    Rlimit rl;
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETRLIMIT),
        RLIMIT_STACK, reinterpret_cast<uint64_t>(&rl), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(rl.rlim_cur > 0);
    JARVIS_ASSERT(rl.rlim_max > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sys_getrlimit_data) {
    Rlimit rl;
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETRLIMIT),
        RLIMIT_DATA, reinterpret_cast<uint64_t>(&rl), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(rl.rlim_cur > 0);
    JARVIS_ASSERT(rl.rlim_max > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sys_getrlimit_invalid) {
    Rlimit rl;
    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::GETRLIMIT),
        99, reinterpret_cast<uint64_t>(&rl), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(sys_brk_query) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::BRK),
        0, 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(cur->program_break, ret);
    JARVIS_TEST_PASS();
}

void register_rlimit_tests() {
    kernel::Logger::info("Registering rlimit tests");
    JARVIS_REGISTER_TEST(sys_getrlimit_nofile);
    JARVIS_REGISTER_TEST(sys_getrlimit_stack);
    JARVIS_REGISTER_TEST(sys_getrlimit_data);
    JARVIS_REGISTER_TEST(sys_getrlimit_invalid);
    JARVIS_REGISTER_TEST(sys_brk_query);
}
