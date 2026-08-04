/*
 * NexIOS RTOS — Development Roadmap / Kernel Core
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
///
///        v0.3.10 rework (SIMULATED → DRIVEN): ceiling locking is performed by
///        a REAL kernel task (prio ≥ 11) in its dispatched lambda.  The
///        holder locks the ceiling mutexes and blocks on a real semaphore;
///        the harness observes the ceiling bookkeeping on the genuinely
///        running task.  No set_current impersonation.

#include <test.hpp>
#include <logger.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/semaphore.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>

using namespace kernel;

namespace {

/// @brief  Context handed to a task lambda via `user_data`.
struct PcpCtx {
    uint64_t m1_;
    uint64_t m2_;
    uint64_t gate_;
};

/// @brief  Create a REAL kernel task (prio ≥ 11) whose lambda locks M1 then
///         M2 (in that order), blocks on the gate, then unlocks M2 then M1.
///         Dispatch and wait for the genuine block (both mutexes held).
/// @param  ceiling1, ceiling2 PCP ceilings for M1 / M2.
TaskControlBlock *spawn_ceiling_holder(sync::Mutex &m1, sync::Mutex &m2,
                                       sync::Semaphore &gate, uint64_t prio,
                                       PcpCtx &ctx) {
    ctx.m1_ = reinterpret_cast<uint64_t>(&m1);
    ctx.m2_ = reinterpret_cast<uint64_t>(&m2);
    ctx.gate_ = reinterpret_cast<uint64_t>(&gate);
    auto *t = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *c = reinterpret_cast<PcpCtx *>(self->user_data);
            auto *mm1 = reinterpret_cast<sync::Mutex *>(c->m1_);
            auto *mm2 = reinterpret_cast<sync::Mutex *>(c->m2_);
            auto *g = reinterpret_cast<sync::Semaphore *>(c->gate_);
            mm1->lock();
            mm2->lock();
            g->wait();
            mm2->unlock();
            mm1->unlock();
        },
        prio, 10);
    if (t == nullptr)
        return nullptr;
    t->user_data = &ctx;
    Scheduler::add_task(*t);
    Scheduler::reschedule();
    while (t->state != TaskState::BLOCKED)
        asm volatile("pause");
    return t;
}

void release_task(TaskControlBlock *t) {
    if (t == nullptr)
        return;
    Scheduler::remove_task(*t);
    t->cleanup();
    delete t;
}

} // namespace

// ---------------------------------------------------------------------------
// PCP — nested ceilings push/pop correctly
// ---------------------------------------------------------------------------
// Task holds mutex A (ceiling=20) then mutex B (ceiling=10).
// held_ceiling_depth_ goes 0 → 1 → 2; system_ceiling_ = max(20,10) = 20.
// On unlock B, depth 1, system_ceiling_ back to 20.  On unlock A, depth 0,
// system_ceiling_ back to 0.  Driven: a REAL task performs the locks in its
// dispatched lambda while blocked on a gate; the harness observes the fields
// on the genuinely-running task.
TEST_CLASS(PcpNestedCeilings) {
    sync::Mutex m_a, m_b;
    m_a.init(20);
    m_b.init(10);
    sync::Semaphore gate;
    gate.init(0, 1);

    PcpCtx ctx{};
    auto *holder = spawn_ceiling_holder(m_a, m_b, gate, 25, ctx);
    CT_ASSERT(holder != nullptr);
    CT_ASSERT(m_a.owner() == holder);
    CT_ASSERT(m_b.owner() == holder);
    CT_ASSERT(holder->held_ceiling_depth_ == 2);
    CT_ASSERT(holder->system_ceiling_ == 20);

    // Release: the task's own lambda unlocks M2 then M1; verify via the
    // post-release state on the terminated task's TCB (fields survive until
    // cleanup — the unlock sequence ran inside the task).
    gate.post();
    while (holder->state != TaskState::TERMINATED)
        asm volatile("pause");

    // After the lambda completed (unlock M2 then M1), the ceiling depth is 0.
    CT_ASSERT(holder->held_ceiling_depth_ == 0);
    CT_ASSERT(holder->system_ceiling_ == 0);
    CT_ASSERT(!m_a.is_locked());
    CT_ASSERT(!m_b.is_locked());

    release_task(holder);
};

// ---------------------------------------------------------------------------
// PCP — ceiling=0 disables PCP (pure PIP)
// ---------------------------------------------------------------------------
// Mutex with ceiling=0 should behave as a normal PIP-only mutex.
// High-pri contender blocks, owner inherits, unlock restores.  Driven: LOW
// holds the mutex (real dispatched lambda), HIGH blocks on it.
TEST_CLASS(PcpCeilingDisabled) {
    sync::Mutex mutex;
    mutex.init(0);
    sync::Semaphore gate;
    gate.init(0, 1);

    struct Ctx {
        uint64_t mutex_;
        uint64_t gate_;
    } ctx;
    ctx.mutex_ = reinterpret_cast<uint64_t>(&mutex);
    ctx.gate_ = reinterpret_cast<uint64_t>(&gate);
    auto *low = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *c = reinterpret_cast<Ctx *>(self->user_data);
            auto *m = reinterpret_cast<sync::Mutex *>(c->mutex_);
            auto *g = reinterpret_cast<sync::Semaphore *>(c->gate_);
            m->lock();
            g->wait();
            m->unlock();
        },
        11, 10);
    CT_ASSERT(low != nullptr);
    low->user_data = &ctx;
    Scheduler::add_task(*low);
    Scheduler::reschedule();
    while (low->state != TaskState::BLOCKED)
        asm volatile("pause");
    CT_ASSERT(mutex.owner() == low);

    // HIGH (prio 20) blocks on the mutex → PIP boosts LOW.
    uint64_t hslot[2];
    uint64_t high_acquired = 0;
    hslot[0] = reinterpret_cast<uint64_t>(&mutex);
    hslot[1] = reinterpret_cast<uint64_t>(&high_acquired);
    auto *high = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *s = reinterpret_cast<uint64_t *>(self->user_data);
            auto *m = reinterpret_cast<sync::Mutex *>(s[0]);
            auto *acq = reinterpret_cast<uint64_t *>(s[1]);
            m->lock();
            __atomic_store_n(acq, 1, __ATOMIC_RELEASE);
            m->unlock();
        },
        20, 10);
    CT_ASSERT(high != nullptr);
    high->user_data = hslot;
    Scheduler::add_task(*high);
    Scheduler::reschedule();
    while (high->state != TaskState::BLOCKED)
        asm volatile("pause");

    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(low->priority >= high->priority);

    gate.post();
    while (low->state != TaskState::TERMINATED ||
           high->state != TaskState::TERMINATED)
        asm volatile("pause");

    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high_acquired == 1);

    release_task(low);
    release_task(high);
};

// ---------------------------------------------------------------------------
// PCP — ceiling below contender still uses PIP fallback
// ---------------------------------------------------------------------------
// Mutex with ceiling=10, low holder (11), high contender (20).  Since high's
// priority > ceiling, normal PIP applies.  Holder inherits, unlock restores.
TEST_CLASS(PcpPipFallback) {
    sync::Mutex mutex;
    mutex.init(10);
    sync::Semaphore gate;
    gate.init(0, 1);

    struct Ctx {
        uint64_t mutex_;
        uint64_t gate_;
    } ctx;
    ctx.mutex_ = reinterpret_cast<uint64_t>(&mutex);
    ctx.gate_ = reinterpret_cast<uint64_t>(&gate);
    auto *low = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *c = reinterpret_cast<Ctx *>(self->user_data);
            auto *m = reinterpret_cast<sync::Mutex *>(c->mutex_);
            auto *g = reinterpret_cast<sync::Semaphore *>(c->gate_);
            m->lock();
            g->wait();
            m->unlock();
        },
        11, 10);
    CT_ASSERT(low != nullptr);
    low->user_data = &ctx;
    Scheduler::add_task(*low);
    Scheduler::reschedule();
    while (low->state != TaskState::BLOCKED)
        asm volatile("pause");
    CT_ASSERT(mutex.owner() == low);

    // HIGH (prio 20) blocks on the mutex → PIP applies (20 > ceiling 10).
    uint64_t hslot[2];
    uint64_t high_acquired = 0;
    hslot[0] = reinterpret_cast<uint64_t>(&mutex);
    hslot[1] = reinterpret_cast<uint64_t>(&high_acquired);
    auto *high = TaskControlBlock::create(
        []() {
            auto *self = Scheduler::current_task();
            auto *s = reinterpret_cast<uint64_t *>(self->user_data);
            auto *m = reinterpret_cast<sync::Mutex *>(s[0]);
            auto *acq = reinterpret_cast<uint64_t *>(s[1]);
            m->lock();
            __atomic_store_n(acq, 1, __ATOMIC_RELEASE);
            m->unlock();
        },
        20, 10);
    CT_ASSERT(high != nullptr);
    high->user_data = hslot;
    Scheduler::add_task(*high);
    Scheduler::reschedule();
    while (high->state != TaskState::BLOCKED)
        asm volatile("pause");

    CT_ASSERT(high->state == TaskState::BLOCKED);
    CT_ASSERT(low->priority >= high->priority);

    gate.post();
    while (low->state != TaskState::TERMINATED ||
           high->state != TaskState::TERMINATED)
        asm volatile("pause");

    CT_ASSERT(low->priority == low->base_priority);
    CT_ASSERT(high_acquired == 1);

    release_task(low);
    release_task(high);
};

void register_mutex_pcp_tests() {
    Logger::info("Registering Mutex PCP tests");
    REGISTER_CLASS(PcpNestedCeilings);
    REGISTER_CLASS(PcpCeilingDisabled);
    REGISTER_CLASS(PcpPipFallback);
}
