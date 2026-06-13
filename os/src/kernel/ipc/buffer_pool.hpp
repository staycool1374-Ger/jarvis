#pragma once

#include <types.hpp>
#include <constants.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/buffer_pool_errors.hpp>

namespace kernel {

enum class ListSentinel : int64_t {
    EMPTY = -1,
};
enum class BufferSentinel : int64_t {
    INVALID_HANDLE = -2,
    INVALID_INDEX  = -3,
};
constexpr int64_t LIST_EMPTY = static_cast<int64_t>(ListSentinel::EMPTY);
constexpr int64_t BUF_INVALID_HANDLE = static_cast<int64_t>(
    BufferSentinel::INVALID_HANDLE);
constexpr int64_t BUF_INVALID_INDEX = static_cast<int64_t>(
    BufferSentinel::INVALID_INDEX);

class BufferPool {
public:
    static constexpr size_t MAX_BUFFERS = 1024;
    static constexpr size_t BUFFER_SIZE = arch::PAGE_SIZE;

    struct Entry {
        uint64_t phys_addr;
        uint32_t generation;
        uint32_t refcount;
        uint32_t owner_task;
        uint64_t mapped_va;
        int32_t  list_prev;
        int32_t  list_next;

        Entry()
            : phys_addr(0)
            , generation(0)
            , refcount(0)
            , owner_task(0)
            , mapped_va(0)
            , list_prev(-1)
            , list_next(-1)
            {}
    };

    /// @brief Initialize the buffer pool (free list, slab allocator).
    static void init();

    /// @brief Allocate a buffer, map it at @p virt_addr in the given
    /// task's address space.
    /// @return handle, or 0 on failure.
    static uint64_t alloc(TaskControlBlock& task, uint64_t virt_addr);

    /// @brief Free a buffer: unmap, deref, return to pool.
    static bool free(TaskControlBlock& task, uint64_t handle);

    /// @brief Map an existing (transferred) buffer at @p virt_addr in
    /// the caller's space.
    static bool map(TaskControlBlock& task, uint64_t handle,
                    uint64_t virt_addr);

    /// @brief Unmap a buffer without freeing it.
    static bool unmap(TaskControlBlock& task, uint64_t handle);

    /// @brief Transfer ownership to another task (used by IPC send).
    static bool transfer(uint64_t handle, TaskControlBlock& from,
                         TaskControlBlock& target_task);

    /// @brief Unmap all buffers owned by a task (called from cleanup/exec).
    static void unmap_all(TaskControlBlock& task);

    /// @brief Validate a handle and return its index, or -1.
    static int32_t validate(uint64_t handle);

    static Entry entries[MAX_BUFFERS];

private:
    static int32_t free_head_;
    static uint32_t next_cookie_;

    static int32_t alloc_entry();
    static void free_entry(int32_t idx);
};

} // namespace kernel
