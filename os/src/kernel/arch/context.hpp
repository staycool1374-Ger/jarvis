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

#pragma once

#include <types.hpp>
#include <constants.hpp>

namespace arch {

/// @brief CPU register save area for context switching.
/// @note Layout matches isr_stubs.asm push order.
struct ArchContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

/// @brief Low-level context management (stack initialisation, save/restore).
class ArchContextManager {
public:
    static inline void init_stack(uint64_t* stack_top, void (*entry)(),
                                  uint64_t cs, uint64_t ss,
                                  uint64_t rflags, uint64_t user_rsp) {
        *--stack_top = ss;
        *--stack_top = user_rsp;
        *--stack_top = rflags;
        *--stack_top = cs;
        *--stack_top = reinterpret_cast<uint64_t>(entry);
        *--stack_top = 0;
        *--stack_top = 0;
        for (int i = 0; i < 15; ++i) *--stack_top = 0;
    }

    static inline void save(ArchContext& ctx, uint64_t rsp) {
        ctx.rsp = rsp;
    }

    static inline void restore(const ArchContext& ctx, uint64_t& rsp) {
        rsp = ctx.rsp;
    }

    static inline void switch_to(ArchContext& from, const ArchContext& to,
                                  uint64_t& rsp) {
        from.rsp = rsp;
        rsp = to.rsp;
    }
};

} // namespace arch
