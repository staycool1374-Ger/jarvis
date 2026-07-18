/// @file ipc_sched_trace.hpp
/// @brief Reusable diagnostic harness for IPC reply-loss (H1) and deferred-
///        switch self-deadlock (H2) analysis in the scheduler/IPC subsystem.
///
/// Everything here is gated behind CONFIG_DEBUG_IPC_SCHED.  Flip the #define
/// in kernel/jarvis_config.h (or uncomment below) to re-enable the whole proof
/// kit for future analysis iterations without re-deriving the instrumentation.
///
/// Proven signatures (what each trace tag proves):
///   [SYNC]        entered IPC::send_sync for (cur -> dest)
///   [SEND]/[WAKE] a message was pushed into dest's queue and dest was woken
///   [SYNC-FAIL] dest-gone-empty  -> genuine peer death, reply never arrived (OK)
///   [SYNC-FAIL] dest-gone-reply  -> H1 PROVEN: reply present in own queue but
///                                    send_sync returned false and discarded it
///   [WEDGE]      H2 candidate: save_rsp_to != 0 while a READY task is orphaned
///                (inrq=1, phys=0) and the ready bitmap is empty -> the timer
///                ISR's rate_monotonic_schedule early-returned on a stale
///                pending-switch flag and never re-dispatched the driver, so
///                the driver could never clear the flag -> self-deadlock.

#pragma once

#include <kernel/arch/serial.hpp>
#include <lib/types.hpp>

#define CONFIG_DEBUG_IPC_SCHED 1

#if defined(CONFIG_DEBUG_IPC_SCHED)

namespace kernel::debug {

/// @brief Emit a raw trace line to the serial console (COM1).
inline void trace(const char *msg) {
    arch::Serial::puts(msg);
    arch::Serial::puts("\n");
}

} // namespace kernel::debug

// Minimal printf-free formatters: build a fixed buffer from integers.
#define IPC_SCHED_TRACE_BUF(tag, n)                                            \
    char _ipc_sched_buf[128];                                                  \
    int _ipc_sched_len = 0;                                                    \
    do {                                                                       \
        const char *_t = (tag);                                                \
        while (*_t)                                                            \
            _ipc_sched_buf[_ipc_sched_len++] = *_t++;                          \
    } while (0)

namespace kernel::debug {
/// @brief Print an integer (decimal, unsigned) appended at buf[pos].
inline int fmt_u64(char *buf, int pos, uint64_t v) {
    char tmp[21];
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v) {
        tmp[i++] = char('0' + (v % 10));
        v /= 10;
    }
    while (i)
        buf[pos++] = tmp[--i];
    return pos;
}
inline void fmt_str(char *buf, int &pos, const char *s) {
    while (*s)
        buf[pos++] = *s++;
}
} // namespace kernel::debug

/// @brief One-shot trace: tag + up to 4 unsigned integers with labels.
#define IPC_SCHED_TRACE(tag, a0, v0, a1, v1, a2, v2, a3, v3)                    \
    do {                                                                       \
        char _b[128];                                                          \
        int _p = 0;                                                            \
        kernel::debug::fmt_str(_b, _p, (tag));                                 \
        kernel::debug::fmt_str(_b, _p, " ");                                   \
        kernel::debug::fmt_str(_b, _p, (a0));                                  \
        _p = kernel::debug::fmt_u64(_b, _p, (v0));                             \
        kernel::debug::fmt_str(_b, _p, " ");                                   \
        kernel::debug::fmt_str(_b, _p, (a1));                                  \
        _p = kernel::debug::fmt_u64(_b, _p, (v1));                             \
        kernel::debug::fmt_str(_b, _p, " ");                                   \
        kernel::debug::fmt_str(_b, _p, (a2));                                  \
        _p = kernel::debug::fmt_u64(_b, _p, (v2));                             \
        kernel::debug::fmt_str(_b, _p, " ");                                   \
        kernel::debug::fmt_str(_b, _p, (a3));                                  \
        _p = kernel::debug::fmt_u64(_b, _p, (v3));                             \
        _b[_p] = 0;                                                            \
        kernel::debug::trace(_b);                                              \
    } while (0)

#else // !CONFIG_DEBUG_IPC_SCHED

#define IPC_SCHED_TRACE(tag, a0, v0, a1, v1, a2, v2, a3, v3)                   \
    do {                                                                       \
    } while (0)

#endif // CONFIG_DEBUG_IPC_SCHED
