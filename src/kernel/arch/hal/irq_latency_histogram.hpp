#pragma once

#include <types.hpp>
#include <kernel/nexios_config.h>

#if CONFIG_IRQ_LATENCY_HISTOGRAM

namespace kernel {

class IrqLatencyHistogram {
public:
    static constexpr size_t BUCKETS = 64;
    static constexpr uint64_t RANGE_NS = 100000;

    static void init();
    static void record(uint64_t entry_tsc);
    static void dump();

private:
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static uint64_t scale_;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static uint64_t buckets_[BUCKETS];
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static uint64_t count_;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static uint64_t max_ns_;
};

} // namespace kernel

#endif // CONFIG_IRQ_LATENCY_HISTOGRAM
