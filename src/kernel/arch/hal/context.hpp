#pragma once

#include <types.hpp>
#include <constants.hpp>

#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

struct ArchContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

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

#elif defined(CONFIG_ARCH_AARCH64)

struct ArchContext {
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29, x30;
    uint64_t sp_el0, elr_el1, spsr_el1;
    uint64_t vector, error_code;
};

class ArchContextManager {
public:
    static inline void init_stack(uint64_t* stack_top, void (*entry)(),
                                  uint64_t /*cs*/, uint64_t /*ss*/,
                                  uint64_t psr, uint64_t user_rsp) {
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = psr;
        *--stack_top = reinterpret_cast<uint64_t>(entry);
        *--stack_top = user_rsp;
        for (int i = 0; i < 31; ++i) *--stack_top = 0;
    }

    static inline void save(ArchContext& ctx, uint64_t rsp) {
        ctx.sp_el0 = rsp;
    }

    static inline void restore(const ArchContext& ctx, uint64_t& rsp) {
        rsp = ctx.sp_el0;
    }

    static inline void switch_to(ArchContext& from, const ArchContext& to,
                                  uint64_t& rsp) {
        from.sp_el0 = rsp;
        rsp = to.sp_el0;
    }
};

#elif defined(CONFIG_ARCH_RISCV64)

struct ArchContext {
    uint64_t ra, sp, gp, tp;
    uint64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64_t sepc, sstatus, stvec;
    uint64_t vector, error_code;
};

class ArchContextManager {
public:
    static inline void init_stack(uint64_t* stack_top, void (*entry)(),
                                  uint64_t /*cs*/, uint64_t /*ss*/,
                                  uint64_t psr, uint64_t user_rsp) {
        *--stack_top = 0;
        *--stack_top = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(entry));
        *--stack_top = user_rsp;
        *--stack_top = psr;
        for (int i = 0; i < 16; ++i) *--stack_top = 0;
    }

    static inline void save(ArchContext& ctx, uint64_t rsp) {
        ctx.sp = rsp;
    }

    static inline void restore(const ArchContext& ctx, uint64_t& rsp) {
        rsp = ctx.sp;
    }

    static inline void switch_to(ArchContext& from, const ArchContext& to,
                                  uint64_t& rsp) {
        from.sp = rsp;
        rsp = to.sp;
    }
};

#else
#  error "HAL: no context implementation for this architecture"
#endif

} // namespace arch
