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

/// @file sporadic_server.cpp
/// @brief Implementation of the Sporadic Server replenishment mechanism.

#include <kernel/task/sporadic_server.hpp>

namespace kernel {
namespace task {

static constexpr uint64_t SPRIO_INVALID = ~0ULL;

void SporadicServer::init(uint64_t budget_c, uint64_t period_t, uint64_t bg_prio) noexcept {
    budget_c_          = budget_c;
    period_t_          = period_t;
    base_priority_     = SPRIO_INVALID;   // caller should set externally
    bg_priority_       = bg_prio;
    budget_remaining_  = budget_c;
    consumed_since_active_ = 0;
    activation_time_   = 0;
    state_             = IDLE;
    replenishment_head_   = 0;
    replenishment_tail_   = 0;
    replenishment_count_  = 0;
}

void SporadicServer::on_activation(uint64_t now) noexcept {
    if (state_ != IDLE)
        return;

    // Server was idle — a new aperiodic job has arrived.
    state_ = ACTIVE;
    activation_time_ = now;
    consumed_since_active_ = 0;

    // budget_remaining_ may be > 0 if prior replenishments have already
    // restored some budget; we do NOT reset it here because that would
    // violate the Sporadic Server budget accounting.  The server simply
    // runs with whatever budget it currently has, and the replenishment
    // at activation_time + T will restore whatever it consumes now.
}

void SporadicServer::on_completion(uint64_t now) noexcept {
    (void)now;
    if (state_ != ACTIVE)
        return;

    // Server goes idle — schedule a replenishment for what was consumed.
    if (consumed_since_active_ > 0) {
        schedule_replenishment(activation_time_ + period_t_, consumed_since_active_);
    }

    // If the server completed with remaining budget, that budget is
    // preserved for the next activation (it does NOT roll over in the
    // standard Sporadic Server model — the replenishment event will
    // restore only the consumed amount, not the full C).
    //
    // However, once we go idle, the next activation will create a new
    // replenishment event, so unused budget is effectively consumed
    // first and the replenishment covers any deficit.
    state_ = IDLE;
    consumed_since_active_ = 0;
}

bool SporadicServer::consume(uint64_t now) noexcept {
    (void)now;
    if (state_ == IDLE)
        return false;

    if (budget_remaining_ == 0) {
        // Budget already exhausted; should not normally be called.
        state_ = EXHAUSTED;
        return false;
    }

    budget_remaining_--;
    consumed_since_active_++;

    if (budget_remaining_ == 0) {
        // Budget exhausted — schedule replenishment and drop to background.
        if (consumed_since_active_ > 0) {
            schedule_replenishment(activation_time_ + period_t_, consumed_since_active_);
        }
        state_ = EXHAUSTED;
        return false;
    }

    return true;
}

void SporadicServer::process_replenishments(uint64_t now) noexcept {
    while (replenishment_count_ > 0) {
        Replenishment r = replenishments_[replenishment_head_];
        if (r.time > now)
            break;

        pop_replenishment();

        // Restore budget, capped at C (can't exceed max budget).
        uint64_t new_budget = budget_remaining_ + r.amount;
        if (new_budget > budget_c_)
            new_budget = budget_c_;
        budget_remaining_ = new_budget;

        // If budget is now positive and server was exhausted, return to
        // normal priority (the caller checks current_priority()).
        if (new_budget > 0 && state_ == EXHAUSTED) {
            // Transition back to ACTIVE so the server can run at its
            // base priority again.  The server may still be mid-job
            // (if budget was exhausted mid-execution) or may be idle
            // waiting for new work.  We conservatively set ACTIVE so
            // the scheduler picks it up; if idle, the next on_completion
            // will put it back.
            state_ = ACTIVE;
        }
    }
}

// ---- Private helpers ----

bool SporadicServer::schedule_replenishment(uint64_t time, uint64_t amount) noexcept {
    if (replenishment_count_ == MAX_REPLENISHMENTS)
        return false;

    replenishments_[replenishment_tail_].time   = time;
    replenishments_[replenishment_tail_].amount = amount;

    replenishment_tail_ = (replenishment_tail_ + 1) % MAX_REPLENISHMENTS;
    replenishment_count_++;
    return true;
}

SporadicServer::Replenishment SporadicServer::pop_replenishment() noexcept {
    Replenishment r = replenishments_[replenishment_head_];
    replenishment_head_ = (replenishment_head_ + 1) % MAX_REPLENISHMENTS;
    replenishment_count_--;
    return r;
}

} // namespace task
} // namespace kernel