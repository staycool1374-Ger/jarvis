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

/// @file test_mutex_pcp.cpp
/// @brief Priority Ceiling Protocol tests for sync::Mutex.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/irq_guard.hpp>

using namespace kernel;

// ---------------------------------------------------------------------------
// PCP — nested ceilings push/pop correctly
// ---------------------------------------------------------------------------
// Task holds mutex A (ceiling=20) then mutex B (ceiling=10).
// held_ceiling_depth_ starts at 0, goes to 1 after A, 2 after B.
// system_ceiling_ = max(20,10) = 20.
// On unlock B, depth goes to 1, system_ceiling_ back to 20.
// On unlock A, depth goes to 0, system_ceiling_ back to 0.
TEST_CLASS(PcpNestedCeilings) {
    arch::IrqGuard irq_guard;
    sync::Mutex m_a, m_b;
    m_a.init(20);
    m_b.init(10);

    auto *original = Scheduler::current_task();

    auto *holder = TaskControlBlock::create([]() {}, 25, 10);
    CT_ASSERT(holder != nullptr);
    holder->base_priority = 25;
    holder->priority = 25;
    Scheduler::add_task(*holder);

    Scheduler::set_current(*holder);
    m_a.lock();
    CT_ASSERT(holder->held_ceiling_depth_ == 1);
    CT_ASSERT(holder->system_ceiling_ == 20);

    m_b.lock();
    CT_ASSERT(holder->held_ceiling_depth_ == 2);
    CT_ASSERT(holder->system_ceiling_ == 20);

    m_b.unlock();
    CT_ASSERT(holder->held_ceiling_depth_ == 1);
    CT_ASSERT(holder->system_ceiling_ == 20);

    m_a.unlock();
    CT_ASSERT(holder->held_ceiling_depth_ == 0);
    CT_ASSERT(holder->system_ceiling_ == 0);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*holder);
    holder->cleanup();
    delete holder;
};

// ---------------------------------------------------------------------------
// PCP — ceiling=0 disables PCP (pure PIP)
// ---------------------------------------------------------------------------
// Mutex with ceiling=0 should behave as a normal PIP-only mutex.
// High-pri contender blocks, owner inherits, unlock restores.
TEST_CLASS(PcpCeilingDisabled) {
    arch::IrqGuard irq_guard;
    sync::Mutex mutex;
    mutex.init(0);

    auto *original = Scheduler::current_task();

    auto *low = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    Scheduler::set_current(*low);
    mutex.lock();
    CT_ASSERT(mutex.owner() == low);

    auto *high = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    mutex.lock();
    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(low->priority >= high->priority);

    Scheduler::set_current(*low);
    mutex.unlock();
    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high->state == TaskState::READY);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
};

// ---------------------------------------------------------------------------
// PCP — ceiling below contender still uses PIP fallback
// ---------------------------------------------------------------------------
// Mutex with ceiling=10, low holder (5), high contender (15).
// Since high's priority > ceiling, normal PIP applies.
// Holder inherits, unlock restores.
TEST_CLASS(PcpPipFallback) {
    arch::IrqGuard irq_guard;
    sync::Mutex mutex;
    mutex.init(10);

    auto *original = Scheduler::current_task();

    auto *low = TaskControlBlock::create([]() {}, 5, 10);
    CT_ASSERT(low != nullptr);
    low->base_priority = 5;
    low->priority = 5;
    Scheduler::add_task(*low);

    Scheduler::set_current(*low);
    mutex.lock();
    CT_ASSERT(mutex.owner() == low);

    auto *high = TaskControlBlock::create([]() {}, 15, 10);
    CT_ASSERT(high != nullptr);
    high->base_priority = 15;
    high->priority = 15;
    Scheduler::add_task(*high);

    Scheduler::set_current(*high);
    mutex.lock();
    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(low->priority >= high->priority);

    Scheduler::set_current(*low);
    mutex.unlock();
    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high->state == TaskState::READY);

    Scheduler::set_current(*original);
    Scheduler::remove_task(*high);
    high->cleanup();
    delete high;
    Scheduler::remove_task(*low);
    low->cleanup();
    delete low;
};

void register_mutex_pcp_tests() {
    Logger::info("Registering Mutex PCP tests");
    REGISTER_CLASS(PcpNestedCeilings);
    REGISTER_CLASS(PcpCeilingDisabled);
    REGISTER_CLASS(PcpPipFallback);
}
