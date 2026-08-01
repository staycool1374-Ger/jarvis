#include "tcb_write_log.hpp"

#include <logger.hpp>
#include <kernel/debug/ipc_sched_trace.hpp>

namespace kernel::diag {

static constexpr size_t WRITE_LOG_DEPTH = 50;
static TcbWriteLog s_tcb_write_log[WRITE_LOG_DEPTH];
static size_t s_tcb_write_idx = 0;
static uint64_t s_tcb_write_seq = 0;

void record_tcb_write(uint64_t tcb_addr, uint64_t field_offset,
                      uint64_t old_value, uint64_t new_value) {
    TcbWriteLog &e = s_tcb_write_log[s_tcb_write_idx % WRITE_LOG_DEPTH];
    e.seq = ++s_tcb_write_seq;
    e.tcb_addr = tcb_addr;
    e.field_offset = field_offset;
    e.old_value = old_value;
    e.new_value = new_value;
    e.caller = __builtin_return_address(0);
    ++s_tcb_write_idx;
}

void dump_tcb_write_log(const char *tag) {
    char buf[32];
    if (tag)
        Logger::raw_write(tag);
    Logger::raw_write("\r\n[TCB-WRITE-LOG] depth=");
    int pos = kernel::debug::fmt_u64(buf, 0, WRITE_LOG_DEPTH);
    buf[pos] = 0;
    Logger::raw_write(buf);
    Logger::raw_write(" recorded=");
    pos = kernel::debug::fmt_u64(buf, 0, s_tcb_write_idx);
    buf[pos] = 0;
    Logger::raw_write(buf);
    Logger::raw_write("\r\n");
    const size_t total = s_tcb_write_idx;
    const size_t start =
        (total < WRITE_LOG_DEPTH) ? 0 : (total - WRITE_LOG_DEPTH);
    for (size_t i = start; i < total; ++i) {
        const TcbWriteLog &e = s_tcb_write_log[i % WRITE_LOG_DEPTH];
        Logger::raw_write("  #");
        pos = kernel::debug::fmt_u64(buf, 0, e.seq);
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write(" tcb=");
        pos = kernel::debug::fmt_u64(buf, 0, e.tcb_addr);
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write(" off=");
        pos = kernel::debug::fmt_u64(buf, 0, e.field_offset);
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write(" old=");
        pos = kernel::debug::fmt_u64(buf, 0, e.old_value);
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write(" new=");
        pos = kernel::debug::fmt_u64(buf, 0, e.new_value);
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write(" caller=");
        pos = kernel::debug::fmt_u64(buf, 0,
                                     reinterpret_cast<uint64_t>(e.caller));
        buf[pos] = 0;
        Logger::raw_write(buf);
        Logger::raw_write("\r\n");
    }
}

} // namespace kernel::diag
