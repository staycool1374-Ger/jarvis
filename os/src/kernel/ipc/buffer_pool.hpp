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

    /// @name Test-isolation helpers (snapshot / restore)
    /// @brief Copy all entries + free_head + next_cookie into flat buffer.
    static void capture_state(uint8_t* dst, size_t max_bytes);
    /// @brief Restore entries + free_head + next_cookie from flat buffer.
    static void restore_state(const uint8_t* src, size_t max_bytes);
    /// @brief Bytes needed for BufferPool state capture.
    static constexpr size_t state_bytes() {
        return sizeof(entries) + sizeof(free_head_) + sizeof(next_cookie_);
    }

private:
    static int32_t free_head_;
    static uint32_t next_cookie_;

    static int32_t alloc_entry();
    static void free_entry(int32_t idx);
};

} // namespace kernel
