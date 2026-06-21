/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <test.hpp>
#include <logger.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: SYS_GETRANDOM fills a buffer with random bytes via syscall
// Input: Syscall::handle(GETRANDOM, buf, 64, 0, ...)
// Expect: Returns 64; buffer not all-zero or all-FF
JARVIS_TEST(syscall_getrandom_basic) {
    uint8_t buf[64];
    __builtin_memset(buf, 0, sizeof(buf));

    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GETRANDOM),
        reinterpret_cast<uint64_t>(buf), 64, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(64ULL, ret);

    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0)  all_zero = false;
        if (buf[i] != 0xFF) all_ff = false;
    }
    JARVIS_ASSERT_FMT(!all_zero, "GETRANDOM returned %zu zero bytes", sizeof(buf));
    JARVIS_ASSERT_FMT(!all_ff, "GETRANDOM returned %zu 0xFF bytes", sizeof(buf));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Zero-length GETRANDOM returns 0 and leaves buffer unchanged
// Input: Syscall::handle(GETRANDOM, buf, 0, 0, ...)
// Expect: Returns 0; buffer unchanged
JARVIS_TEST(syscall_getrandom_zero) {
    uint8_t buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GETRANDOM),
        reinterpret_cast<uint64_t>(buf), 0, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(0ULL, ret);
    JARVIS_ASSERT(buf[0] == 0xDE && buf[1] == 0xAD && buf[2] == 0xBE && buf[3] == 0xEF);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Large GETRANDOM (4096 bytes) succeeds without overflow
// Input: Syscall::handle(GETRANDOM, buf, 4096, 0, ...)
// Expect: Returns 4096; first and last bytes not zero (statistical)
JARVIS_TEST(syscall_getrandom_large) {
    uint8_t buf[4096];
    __builtin_memset(buf, 0, sizeof(buf));

    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GETRANDOM),
        reinterpret_cast<uint64_t>(buf), 4096, 0, 0, nullptr);
    JARVIS_ASSERT_EQ(4096ULL, ret);

    bool any_nonzero = false;
    for (size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0) { any_nonzero = true; break; }
    }
    JARVIS_ASSERT_FMT(any_nonzero, "GETRANDOM(4096) returned all zeros");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Non-zero flags to GETRANDOM returns -1 (EINVAL)
// Input: Syscall::handle(GETRANDOM, buf, 8, 1, ...) — flags=1
// Expect: Returns static_cast<uint64_t>(-1)
JARVIS_TEST(syscall_getrandom_invalid_flags) {
    uint8_t buf[8];
    uint64_t ret = Syscall::handle(
        static_cast<uint64_t>(SyscallNumber::GETRANDOM),
        reinterpret_cast<uint64_t>(buf), 8, 1, 0, nullptr);
    JARVIS_ASSERT_EQ(static_cast<uint64_t>(-1), ret);
    JARVIS_TEST_PASS();
}

void register_random_syscall_tests() {
    Logger::info("Registering SYS_GETRANDOM tests");
    JARVIS_REGISTER_TEST(syscall_getrandom_basic);
    JARVIS_REGISTER_TEST(syscall_getrandom_zero);
    JARVIS_REGISTER_TEST(syscall_getrandom_large);
    JARVIS_REGISTER_TEST(syscall_getrandom_invalid_flags);
}
