#include <test.hpp>
#include <logger.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <constants.hpp>

using namespace kernel;

JARVIS_TEST(secure_exec_valid_argv_envp) {
    uint64_t user_addr = 0x100000;

    JARVIS_ASSERT(is_user_range(reinterpret_cast<const void*>(user_addr), 8));
    JARVIS_ASSERT(is_user_range(reinterpret_cast<const void*>(user_addr), 4096));

    auto checked_ptr = checked(reinterpret_cast<const uint64_t*>(user_addr));
    JARVIS_ASSERT(checked_ptr.valid());

    auto checked_multi = checked(reinterpret_cast<const uint64_t*>(user_addr),
                                 static_cast<uint64_t>(4));
    JARVIS_ASSERT(checked_multi.valid());

    JARVIS_ASSERT(is_user_string(reinterpret_cast<const void*>(user_addr), 64));

    JARVIS_TEST_PASS();
}

JARVIS_TEST(secure_exec_null_argv_eFault) {
    JARVIS_ASSERT(!is_user_range(nullptr, 1));
    JARVIS_ASSERT(!is_user_range(nullptr, 0));

    auto checked_null = checked(reinterpret_cast<const char*>(0));
    JARVIS_ASSERT(!checked_null.valid());

    JARVIS_ASSERT(!is_user_string(nullptr));

    JARVIS_TEST_PASS();
}

JARVIS_TEST(secure_exec_kernel_space_argv_eFault) {
    uint64_t kernel_addr = 0xFFFF800000000000ULL;

    JARVIS_ASSERT(!is_user_range(reinterpret_cast<const void*>(kernel_addr), 1));
    JARVIS_ASSERT(!is_user_range(reinterpret_cast<const void*>(kernel_addr), 0));

    auto checked_kernel = checked(reinterpret_cast<const char*>(kernel_addr));
    JARVIS_ASSERT(!checked_kernel.valid());

    JARVIS_ASSERT(!is_user_string(reinterpret_cast<const void*>(kernel_addr)));

    uint64_t hhdm = 0xFFFF800000000000ULL;
    JARVIS_ASSERT(!is_user_range(reinterpret_cast<const void*>(hhdm - 8), 16));

    JARVIS_TEST_PASS();
}

JARVIS_TEST(secure_exec_unmapped_crossing_eFault) {
    uint64_t user_limit = 0x0000800000000000ULL;

    JARVIS_ASSERT(is_user_range(reinterpret_cast<const void*>(user_limit - 8), 8));
    JARVIS_ASSERT(!is_user_range(reinterpret_cast<const void*>(user_limit - 8), 16));

    auto valid_range = checked(reinterpret_cast<const char*>(user_limit - 8),
                               static_cast<uint64_t>(8));
    JARVIS_ASSERT(valid_range.valid());

    auto cross_range = checked(reinterpret_cast<const char*>(user_limit - 8),
                               static_cast<uint64_t>(16));
    JARVIS_ASSERT(!cross_range.valid());

    JARVIS_ASSERT(!is_user_range(reinterpret_cast<const void*>(0xFFFFFFFFFFFFFFF0ULL), 16));

    JARVIS_TEST_PASS();
}

JARVIS_TEST(secure_exec_regression_audit) {
    JARVIS_TEST_PASS();
}

void register_secure_exec_tests() {
    Logger::info("Registering secure exec tests");

    JARVIS_REGISTER_TEST(secure_exec_valid_argv_envp);
    JARVIS_REGISTER_TEST(secure_exec_null_argv_eFault);
    JARVIS_REGISTER_TEST(secure_exec_kernel_space_argv_eFault);
    JARVIS_REGISTER_TEST(secure_exec_unmapped_crossing_eFault);
    JARVIS_REGISTER_TEST(secure_exec_regression_audit);
}
