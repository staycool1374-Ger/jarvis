#pragma once

/// @file test_expected_counts.hpp
/// @brief Expected test-case count tables per architecture.

#include <types.hpp>
#include <logger.hpp>

namespace kernel::test {

struct ExpectedCounts {
    const char* name;
    size_t x86_64;
    size_t aarch64;
    size_t riscv64;
};

static constexpr ExpectedCounts k_expected_counts[] = {
    // Class name           x86_64  aarch64  riscv64
    {"safe",                132,    0,       0      },  // curated TF_RELEASE subset
    {"selftest",            132,    0,       0      },  // same as safe
    {"all",                 741,    0,       0      },  // every registration function
    {"scheduler",           85,     0,       0      },  // sched + task + lifecycle + deadlock + timer
    {"memory",              45,     0,       0      },  // PMM + VMM + checked_ptr + buffer_pool
    {"ipc",                 42,     0,       0      },  // IPC + pipe + blocking + lock-free + robustness
    {"vfs",                 143,    0,       0      },  // vfs + tmpfs + fat32 + block + fstab + sync + vfsd + iocd
    {"process",             43,     0,       0      },  // process + elf + signals + rlimit + waitpid + pml4_clone
    {"syscall",             28,     0,       0      },  // syscall + syscall_fuzz
    {"arch",                59,     0,       0      },  // cross_arch + GDT + IDT + bootparams + multiboot + address + PIC + HAL
    {"cross_arch",          16,     0,       0      },  // cross-architecture tests
    {"device",              33,     0,       0      },  // serial + keyboard + spsc + irq_guard + framebuffer + rtc + driver
    {"shell",               22,     0,       0      },  // shell_interaction + shell_redirect + textutils
    {"net",                 42,     0,       0      },  // net + PCI + virtio + DMA
    {"security",            31,     0,       0      },  // capability + secure_exec + vfsd_authorization
    {"debug",               14,     0,       0      },  // debug + gcov + klog
    {"integration",          1,     0,       0      },  // integration smoke tests
    {"starvation_deadlock",  3,     0,       0      },  // SchedulerStarvation + PriorityInversionChain5 + DeadlockNestedMutexLoad
    {"deadline_miss",        3,     0,       0      },  // DeadlineMissWhileBlocked + DeadlineMissWhileTerminatedSkipped + DeadlineRearmOnPeriodRollover
    {"stress",               9,     0,       0      },  // 6 stress + 3 starvation_deadlock
    {"init",                 3,     0,       0      },  // init tests
    {"build",                5,     0,       0      },  // buildsystem tests
    {"bench",               17,     0,       0      },  // IPC + microkernel + syscall/IRQ latency benchmarks
    {"sporadic",            25,     0,       0      },  // sporadic server tests
    {"atomic",              12,     0,       0      },  // atomic operation tests
};

static constexpr size_t k_expected_count_size =
    sizeof(k_expected_counts) / sizeof(k_expected_counts[0]);

inline size_t arch_count(const ExpectedCounts& ec) {
#if defined(CONFIG_ARCH_X86_64)
    return ec.x86_64;
#elif defined(CONFIG_ARCH_AARCH64)
    return ec.aarch64;
#elif defined(CONFIG_ARCH_RISCV64)
    return ec.riscv64;
#else
    return ec.x86_64;
#endif
}

inline size_t expected_for_class(const char* name) {
    for (size_t i = 0; i < k_expected_count_size; ++i) {
        if (__builtin_strcmp(name, k_expected_counts[i].name) == 0) {
            return arch_count(k_expected_counts[i]);
        }
    }
    return 0;
}

inline bool validate_class_count(const char* name, size_t actual_count) {
    size_t expected = expected_for_class(name);
    if (expected == 0) {
        return true;
    }
    if (actual_count != expected) {
        Logger::warn("[TCOUNT] MISMATCH class=%s expected=%u actual=%u "
                     " -- update test_expected_counts.hpp",
                     name, (unsigned)expected, (unsigned)actual_count);
        return false;
    }
    return true;
}

inline void validate_all_consistency() {
    size_t all_count = 0;
    size_t sum_individual = 0;
    for (size_t i = 0; i < k_expected_count_size; ++i) {
        size_t c = arch_count(k_expected_counts[i]);
        if (__builtin_strcmp(k_expected_counts[i].name, "all") == 0) {
            all_count = c;
        } else if (__builtin_strcmp(k_expected_counts[i].name, "safe") != 0) {
            sum_individual += c;
        }
    }
    if (all_count > 0 && sum_individual < all_count) {
        Logger::warn("[TCOUNT] CONSISTENCY: sum(individual)=%u < all=%u -- "
                     "some tests missing from individual class entries",
                     (unsigned)sum_individual, (unsigned)all_count);
    } else if (all_count > 0) {
        Logger::info("[TCOUNT] CONSISTENCY: sum(individual)=%u >= all=%u (overlap=%u)",
                     (unsigned)sum_individual, (unsigned)all_count,
                     (unsigned)(sum_individual - all_count));
    }
}

} // namespace kernel::test