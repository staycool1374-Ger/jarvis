#include <kernel/arch/irq_latency_histogram.hpp>

#if CONFIG_IRQ_LATENCY_HISTOGRAM

#include <logger.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/timer.hpp>

namespace kernel {

uint64_t IrqLatencyHistogram::scale_;
uint64_t IrqLatencyHistogram::buckets_[BUCKETS];
uint64_t IrqLatencyHistogram::count_;
uint64_t IrqLatencyHistogram::max_ns_;

void IrqLatencyHistogram::init() {
    for (auto &b : buckets_)
        b = 0;
    count_ = 0;
    max_ns_ = 0;

    uint64_t tsc_freq = arch::Timer::tsc_freq_hz();
    uint64_t ticks_per_100us = tsc_freq / 10000;
    scale_ = ticks_per_100us > 0 ? ticks_per_100us : 1;
}

void IrqLatencyHistogram::record(uint64_t entry_tsc) {
    uint64_t now = arch::rdtsc();
    uint64_t delta = now - entry_tsc;

    uint64_t ns = delta * 1000000000ULL / arch::Timer::tsc_freq_hz();
    if (ns > max_ns_)
        max_ns_ = ns;

    uint64_t idx = delta / scale_;
    if (idx >= BUCKETS)
        idx = BUCKETS - 1;
    buckets_[idx]++;
    count_++;

#if CONFIG_IRQ_LATENCY_MAX_NS > 0
    if (ns > CONFIG_IRQ_LATENCY_MAX_NS) {
        Logger::fatal("IRQ latency %lu ns exceeds CONFIG_IRQ_LATENCY_MAX_NS (%lu)",
                      ns, static_cast<uint64_t>(CONFIG_IRQ_LATENCY_MAX_NS));
        IrqLatencyHistogram::dump();
        panic("IRQ latency exceeded");
    }
#endif
}

void IrqLatencyHistogram::dump() {
    Logger::info("IRQ latency histogram (%lu samples, max %lu ns):", count_, max_ns_);
    for (size_t i = 0; i < BUCKETS; ++i) {
        if (buckets_[i] > 0) {
            uint64_t lo = i * RANGE_NS / BUCKETS;
            uint64_t hi = (i + 1) * RANGE_NS / BUCKETS;
            Logger::info("  [%lu-%lu ns]: %lu", lo, hi, buckets_[i]);
        }
    }
}

} // namespace kernel

#endif // CONFIG_IRQ_LATENCY_HISTOGRAM
