/*
 * Jarvis RTOS — Kernel Log (dmesg) Ring Buffer
 * Lock-free SPSC ring buffer for structured kernel logging.
 */

#pragma once

#include <types.hpp>
#include <lib/error.hpp>
#include <kernel/sync/sync_errors.hpp>
#include <kernel/vfs/vfs_errors.hpp>
#include <kernel/memory/mempool_errors.hpp>
#include <kernel/task/scheduler_errors.hpp>
#include <kernel/ipc/ipc_errors.hpp>
#include <kernel/syscall/syscall_errors.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/jarvis_config.h>
#include <lib/atomic.hpp>

namespace kernel::log {

enum class ErrorSubsystem : uint8_t {
    BASE    = 0,
    SYNC    = 1,
    VFS     = 2,
    MEMPOOL = 3,
    SCHED   = 4,
    IPC     = 5,
    SYSCALL = 6,
};

struct LogEntry {
    uint64_t timestamp;
    uint64_t task_id;
    ErrorSubsystem subsystem;
    uint64_t error_code;
    uintptr_t context;
    const char* message;
};

template<size_t Capacity>
class DmesgBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    LogEntry buffer[Capacity];
    alignas(64) volatile size_t head{0};
    alignas(64) volatile size_t tail{0};

public:
    bool push(ErrorSubsystem subsys, uint64_t err_code, const char* msg, uintptr_t ctx = 0);
    bool pop(LogEntry& entry);

    template<typename Fn>
    void for_each(Fn&& fn) const {
        size_t t = atomic_load(&tail, __ATOMIC_ACQUIRE);
        size_t h = atomic_load(&head, __ATOMIC_ACQUIRE);

        while (t != h) {
            fn(buffer[t]);
            t = (t + 1) & MASK;
        }
    }

    bool empty() const;
    size_t size() const;
    void clear();

    size_t head_index() const { return head; }
    size_t tail_index() const { return tail; }
};

constexpr size_t DMESG_CAPACITY = CONFIG_DMESG_CAPACITY;
extern DmesgBuffer<DMESG_CAPACITY> g_dmesg;

#define dmesg_push(subsys, err, msg, ctx) \
    kernel::log::g_dmesg.push(kernel::log::ErrorSubsystem::subsys, \
                              static_cast<uint64_t>(err), msg, ctx)

#define dmesg_push_base(err, msg, ctx) \
    kernel::log::g_dmesg.push(kernel::log::ErrorSubsystem::BASE, \
                              static_cast<uint64_t>(err), msg, ctx)

inline const char* base_error_string(kernel::Error e) {
    switch (e) {
        case kernel::Error::OK: return "OK";
        case kernel::Error::OOM: return "Out of memory";
        case kernel::Error::INVALID_ARG: return "Invalid argument";
        case kernel::Error::NOT_FOUND: return "Not found";
        case kernel::Error::ALREADY_EXISTS: return "Already exists";
        case kernel::Error::TIMEOUT: return "Timeout";
        case kernel::Error::BUSY: return "Busy";
        case kernel::Error::NOT_IMPLEMENTED: return "Not implemented";
        case kernel::Error::IO_ERROR: return "I/O error";
        case kernel::Error::CORRUPTED: return "Corrupted";
    }
    return "Unknown base error";
}

inline const char* subsystem_name(ErrorSubsystem s) {
    switch (s) {
        case ErrorSubsystem::BASE:    return "BASE";
        case ErrorSubsystem::SYNC:    return "SYNC";
        case ErrorSubsystem::VFS:     return "VFS";
        case ErrorSubsystem::MEMPOOL: return "MPOOL";
        case ErrorSubsystem::SCHED:   return "SCHED";
        case ErrorSubsystem::IPC:     return "IPC";
        case ErrorSubsystem::SYSCALL: return "SYSCALL";
        default: return "UNK";
    }
}

inline const char* error_string(ErrorSubsystem subsys, uint64_t code) {
    switch (subsys) {
        case ErrorSubsystem::BASE:
            return base_error_string(static_cast<kernel::Error>(code));
        case ErrorSubsystem::SYNC:
            return kernel::errors::error_string(static_cast<kernel::errors::SyncError>(code));
        case ErrorSubsystem::VFS:
            return kernel::errors::error_string(static_cast<kernel::errors::VfsError>(code));
        case ErrorSubsystem::MEMPOOL:
            return kernel::errors::error_string(static_cast<kernel::errors::MemPoolError>(code));
        case ErrorSubsystem::SCHED:
            return kernel::errors::error_string(static_cast<kernel::errors::SchedulerError>(code));
        case ErrorSubsystem::IPC:
            return kernel::errors::error_string(static_cast<kernel::errors::IpcError>(code));
        case ErrorSubsystem::SYSCALL:
            return kernel::errors::error_string(static_cast<kernel::errors::SyscallError>(code));
        default:
            return "UNKNOWN";
    }
}

} // namespace kernel::log