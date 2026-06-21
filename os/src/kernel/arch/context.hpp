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
