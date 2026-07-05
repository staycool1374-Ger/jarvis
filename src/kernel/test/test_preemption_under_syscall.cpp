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

/// @file test_preemption_under_syscall.cpp
/// @brief Preemption during system call tests.

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/tmpfs.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/arch/io.hpp>

using namespace kernel;
using namespace kernel::vfs;

static volatile uint64_t preempt_highpri_count = 0;
static volatile uint64_t preempt_lowpri_count = 0;
static volatile bool preempt_highpri_done = false;

// --- tmpfs write preemption ---
static volatile uint64_t tp_highpri_count_ = 0;
static volatile bool tp_done_ = false;

static void tp_highpri_task() {
    __atomic_add_fetch(&tp_highpri_count_, 1, __ATOMIC_RELAXED);
    tp_done_ = true;
}

static void tp_lowpri_write() {
    Vnode* file = vfs::resolve("/tmp_preempt_prio/data.bin");
    if (!file) return;
    const char buf[64] = {0};
    for (uint64_t i = 0; i < 50 && !tp_done_; ++i) {
        file->ops->write(*file, reinterpret_cast<const uint8_t*>(buf), 64, 0);
        for (uint64_t w = 0; w < 500; ++w) arch::pause();
    }
}

// --- brk preemption ---
static volatile uint64_t brk_highpri_count_ = 0;

static void brk_highpri_task() {
    __atomic_add_fetch(&brk_highpri_count_, 1, __ATOMIC_RELAXED);
}

static volatile bool brk_done_ = false;

static void brk_lowpri_task() {
    for (uint64_t i = 0; i < 20 && !brk_done_; ++i) {
        uint64_t ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::BRK),
            0, 0, 0, 0, nullptr);
        (void)ret;
        for (uint64_t w = 0; w < 1000; ++w) arch::pause();
    }
}

// --- starvation test ---
static volatile uint64_t starve_highpri_count_ = 0;
static volatile uint64_t starve_lowpri_progress_ = 0;
static constexpr uint64_t STARVE_TARGET = 50;

static void starve_highpri_task() {
    __atomic_add_fetch(&starve_highpri_count_, 1, __ATOMIC_RELAXED);
}

static void starve_lowpri_task() {
    for (uint64_t i = 0; i < STARVE_TARGET; ++i) {
        __atomic_store_n(&starve_lowpri_progress_, i + 1, __ATOMIC_RELEASE);
        for (uint64_t w = 0; w < 200; ++w) arch::pause();
    }
}

static void preempt_highpri_task() {
    __atomic_add_fetch(&preempt_highpri_count, 1, __ATOMIC_RELAXED);
    preempt_highpri_done = true;
}

static void preempt_lowpri_syscall() {
    for (uint64_t i = 0; i < 1000000; ++i) {
        arch::pause();
        __atomic_add_fetch(&preempt_lowpri_count, 1, __ATOMIC_RELAXED);
        if (preempt_highpri_done) break;
    }
}

// Runmode: kernel
// Testidea: High-priority task preempts a low-priority task inside a long syscall.
// Input: Two tasks: low-pri (5) runs a long loop, high-pri (10) should preempt it.
// Expect: High-pri task runs at least once.
// Depends: Scheduler, context-switch atomics
JARVIS_TEST(preemption_during_syscall, "PRE: none | POST: none") {
    preempt_highpri_count = 0;
    preempt_lowpri_count = 0;
    preempt_highpri_done = false;

    auto* high = TaskControlBlock::create(preempt_highpri_task, 10, 1);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* low = TaskControlBlock::create(preempt_lowpri_syscall, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    low->user_data = const_cast<uint64_t*>(&preempt_highpri_count);
    Scheduler::add_task(*low);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*low);
        low->cleanup();
        delete low;
        Scheduler::remove_task(*high);
        high->cleanup();
        delete high;
    });

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);

    for (int tick = 0; tick < 10 && !preempt_highpri_count; ++tick) {
        low->state = TaskState::RUNNING;
        Scheduler::on_tick();
    }
    // Yield to let the timer ISR consume pending context-switch globals
    for (int h = 0; h < 6 && !preempt_highpri_count; ++h) arch::hlt();

    Scheduler::set_current(*original);

    JARVIS_ASSERT_FMT(preempt_highpri_count > 0,
                      "High-pri task ran %lu times (expected > 0)", preempt_highpri_count);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: High-pri task preempts low-pri task during tmpfs file write.
// Input: Low-pri (5) writes to tmpfs; high-pri (10) runs concurrently.
// Expect: High-pri runs at least once, proving preemption during VFS write.
// Depends: tmpfs, Scheduler, VFS
JARVIS_TEST(preempt_highpri_during_tmpfs_write, "PRE: vfsd, iocd | POST: none") {
    tp_highpri_count_ = 0;
    tp_done_ = false;

    int ret = vfs::mount(tmpfs_fs, "/tmp_preempt_prio");
    JARVIS_ASSERT_EQ(0, ret);
    ret = vfs::create("/tmp_preempt_prio/data.bin", 0);
    JARVIS_ASSERT_EQ(0, ret);

    auto* high = TaskControlBlock::create(tp_highpri_task, 10, 1);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* low = TaskControlBlock::create(tp_lowpri_write, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    Scheduler::add_task(*low);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*high);
        high->cleanup();
        delete high;
        Scheduler::remove_task(*low);
        low->cleanup();
        delete low;
    });

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);

    for (int tick = 0; tick < 20 && !tp_done_; ++tick) {
        low->state = TaskState::RUNNING;
        Scheduler::on_tick();
    }
    // Yield to let the timer ISR fire and run the scheduled tasks
    for (int h = 0; h < 6 && !tp_done_; ++h) arch::hlt();

    Scheduler::set_current(*original);

    JARVIS_ASSERT_FMT(tp_highpri_count_ > 0,
        "High-pri ran %lu times during tmpfs write (expected > 0)",
        tp_highpri_count_);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: High-pri task preempts low-pri task during sys_brk call.
// Input: Low-pri (5) calls sys_brk in loop; high-pri (10) runs concurrently.
// Expect: High-pri runs at least once during brk operations.
// Depends: Syscall::sys_brk, Scheduler
JARVIS_TEST(preempt_highpri_during_brk, "PRE: none | POST: none") {
    brk_highpri_count_ = 0;
    brk_done_ = false;

    auto* high = TaskControlBlock::create(brk_highpri_task, 10, 1);
    JARVIS_ASSERT(high != nullptr);
    Scheduler::add_task(*high);

    auto* low = TaskControlBlock::create(brk_lowpri_task, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    Scheduler::add_task(*low);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*high);
        high->cleanup();
        delete high;
        Scheduler::remove_task(*low);
        low->cleanup();
        delete low;
    });

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);

    for (int tick = 0; tick < 15; ++tick) {
        low->state = TaskState::RUNNING;
        Scheduler::on_tick();
    }
    for (int h = 0; h < 6 && !brk_done_; ++h) arch::hlt();

    Scheduler::set_current(*original);

    JARVIS_ASSERT_FMT(brk_highpri_count_ > 0,
        "High-pri ran %lu times during brk (expected > 0)",
        brk_highpri_count_);

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Low-priority task is not starved by higher-priority tasks.
// Input: Low-pri (5) loops to STARVE_TARGET; 2 high-pri (10) tasks also run.
// Expect: Low-pri reaches STARVE_TARGET despite higher-priority competition.
// Depends: Scheduler
JARVIS_TEST(preempt_lowpri_not_starved, "PRE: none | POST: none") {
    starve_highpri_count_ = 0;
    starve_lowpri_progress_ = 0;

    auto* high_a = TaskControlBlock::create(starve_highpri_task, 10, 1);
    JARVIS_ASSERT(high_a != nullptr);
    Scheduler::add_task(*high_a);

    auto* high_b = TaskControlBlock::create(starve_highpri_task, 10, 1);
    JARVIS_ASSERT(high_b != nullptr);
    Scheduler::add_task(*high_b);

    auto* low = TaskControlBlock::create(starve_lowpri_task, 5, 10);
    JARVIS_ASSERT(low != nullptr);
    Scheduler::add_task(*low);

    auto guard = ScopeGuard([&]() {
        Scheduler::remove_task(*high_a);
        high_a->cleanup();
        delete high_a;
        Scheduler::remove_task(*high_b);
        high_b->cleanup();
        delete high_b;
        Scheduler::remove_task(*low);
        low->cleanup();
        delete low;
    });

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*low);

    for (int tick = 0; tick < 100 && starve_lowpri_progress_ < STARVE_TARGET; ++tick) {
        low->state = TaskState::RUNNING;
        Scheduler::on_tick();
    }
    for (int h = 0; h < 20 && starve_lowpri_progress_ < STARVE_TARGET; ++h) arch::hlt();

    Scheduler::set_current(*original);

    JARVIS_ASSERT_FMT(starve_lowpri_progress_ == STARVE_TARGET,
        "Low-pri progress %lu (expected %lu), high-pri ran %lu/%lu times",
        starve_lowpri_progress_, STARVE_TARGET,
        starve_highpri_count_, 2ULL);

    JARVIS_TEST_PASS();
}

void register_preemption_under_syscall_tests() {
    Logger::info("Registering preemption-under-syscall tests");
    JARVIS_REGISTER_TEST(preemption_during_syscall);
    JARVIS_REGISTER_TEST(preempt_highpri_during_tmpfs_write);
    JARVIS_REGISTER_TEST(preempt_highpri_during_brk);
    JARVIS_REGISTER_TEST(preempt_lowpri_not_starved);
}
