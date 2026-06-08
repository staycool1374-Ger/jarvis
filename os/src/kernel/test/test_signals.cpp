#include <test.hpp>
#include <logger.hpp>
#include <signal.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/syscall/syscall.hpp>

using namespace kernel;

static void test_signal_handler(int sig);

JARVIS_TEST(signal_exception_to_signal_mapping) {
    auto map0 = exception_to_signal(0);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(Signal::SIGFPE), static_cast<uint64_t>(map0.signal));
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(SignalAction::TERMINATE), static_cast<uint64_t>(map0.action));

    auto map6 = exception_to_signal(6);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(Signal::SIGILL), static_cast<uint64_t>(map6.signal));

    auto map13 = exception_to_signal(13);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(Signal::SIGSEGV), static_cast<uint64_t>(map13.signal));

    auto map14 = exception_to_signal(14);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(Signal::SIGSEGV), static_cast<uint64_t>(map14.signal));

    auto map16 = exception_to_signal(16);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(Signal::SIGFPE), static_cast<uint64_t>(map16.signal));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_is_fatal_check) {
    JARVIS_ASSERT(signal_is_fatal(static_cast<uint64_t>(Signal::SIGKILL)));
    JARVIS_ASSERT(!signal_is_fatal(static_cast<uint64_t>(Signal::SIGSEGV)));
    JARVIS_ASSERT(!signal_is_fatal(static_cast<uint64_t>(Signal::SIGFPE)));
    JARVIS_ASSERT(!signal_is_fatal(static_cast<uint64_t>(Signal::SIGILL)));
    JARVIS_ASSERT(!signal_is_fatal(static_cast<uint64_t>(Signal::SIGTERM)));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_default_action) {
    auto term = default_signal_action(static_cast<uint64_t>(Signal::SIGSEGV));
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(SignalAction::TERMINATE), static_cast<uint64_t>(term));

    auto ignore = default_signal_action(static_cast<uint64_t>(Signal::SIGCHLD));
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(SignalAction::IGNORE), static_cast<uint64_t>(ignore));

    auto fatal = default_signal_action(static_cast<uint64_t>(Signal::SIGKILL));
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(SignalAction::TERMINATE), static_cast<uint64_t>(fatal));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_handler_tcb_registration) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    JARVIS_ASSERT(!cur->has_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV)));

    auto handler = reinterpret_cast<sighandler_t>(static_cast<uintptr_t>(0xDEAD));
    cur->set_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV), handler);
    JARVIS_ASSERT(cur->has_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV)));
    JARVIS_ASSERT_EQ(reinterpret_cast<uint64_t>(handler),
                     reinterpret_cast<uint64_t>(cur->get_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV))));

    cur->set_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV), nullptr);
    JARVIS_ASSERT(!cur->has_signal_handler(static_cast<uint64_t>(Signal::SIGSEGV)));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_handler_out_of_bounds) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    cur->set_signal_handler(100, reinterpret_cast<sighandler_t>(static_cast<uintptr_t>(0xFF)));
    JARVIS_ASSERT(!cur->has_signal_handler(100));

    cur->set_signal_handler(31, nullptr);
    JARVIS_ASSERT(!cur->has_signal_handler(31));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_pending_bitmask) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    JARVIS_ASSERT_EQ(0ULL, cur->pending_signals);

    cur->pending_signals |= (1ULL << static_cast<uint64_t>(Signal::SIGSEGV));
    JARVIS_ASSERT(cur->pending_signals & (1ULL << static_cast<uint64_t>(Signal::SIGSEGV)));

    cur->pending_signals &= ~(1ULL << static_cast<uint64_t>(Signal::SIGSEGV));
    JARVIS_ASSERT_EQ(0ULL, cur->pending_signals);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_kill_delivers) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    cur->set_signal_handler(static_cast<uint64_t>(Signal::SIGUSR1), test_signal_handler);

    JARVIS_ASSERT_EQ(0ULL, cur->pending_signals);

    uint64_t ret = Syscall::handle(static_cast<uint64_t>(SyscallNumber::KILL), cur->id, static_cast<uint64_t>(Signal::SIGUSR1), 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);

    JARVIS_ASSERT(cur->pending_signals & (1ULL << static_cast<uint64_t>(Signal::SIGUSR1)));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(signal_handler_invoked) {
    auto* cur = Scheduler::current_task();
    JARVIS_ASSERT(cur != nullptr);

    cur->set_signal_handler(static_cast<uint64_t>(Signal::SIGUSR1), test_signal_handler);
    JARVIS_ASSERT(cur->has_signal_handler(static_cast<uint64_t>(Signal::SIGUSR1)));

    cur->pending_signals |= (1ULL << static_cast<uint64_t>(Signal::SIGUSR1));
    JARVIS_TEST_PASS();
}

static void test_signal_handler(int sig) {
    (void)sig;
}

void register_signals_tests() {
    Logger::info("Registering signal tests");

    JARVIS_REGISTER_TEST(signal_exception_to_signal_mapping);
    JARVIS_REGISTER_TEST(signal_is_fatal_check);
    JARVIS_REGISTER_TEST(signal_default_action);
    JARVIS_REGISTER_TEST(signal_handler_tcb_registration);
    JARVIS_REGISTER_TEST(signal_handler_out_of_bounds);
    JARVIS_REGISTER_TEST(signal_pending_bitmask);
    JARVIS_REGISTER_TEST(signal_kill_delivers);
    JARVIS_REGISTER_TEST(signal_handler_invoked);
}
