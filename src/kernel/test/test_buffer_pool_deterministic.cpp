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

#include <test.hpp>
#include <logger.hpp>
#include <scope_guard.hpp>
#include <kernel/test/task_ptr.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>

using namespace kernel;

JARVIS_TEST(buffer_pool_deterministic_preallocated_pool,
            "PRE: none | POST: none") {
    JARVIS_ASSERT(BufferPool::POOL_PAGES == CONFIG_BUFFER_POOL_PAGES);
    JARVIS_ASSERT(BufferPool::POOL_PAGES > 0);
    JARVIS_ASSERT(BufferPool::MAX_BUFFERS == 1024);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_deterministic_no_dynamic_alloc,
            "PRE: none | POST: none") {
    SimpleTaskPtr task(TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB));
    JARVIS_ASSERT(task != nullptr);
    uint64_t handle = BufferPool::alloc(*task, 0x10000000);
    JARVIS_ASSERT(handle != 0);
    uint32_t idx = static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
    JARVIS_ASSERT(BufferPool::entries[idx].phys_addr != 0);
    JARVIS_ASSERT(BufferPool::entries[idx].mapped_va == 0x10000000);
    JARVIS_ASSERT(BufferPool::free(*task, handle));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_deterministic_exhaustion_returns_zero,
            "PRE: none | POST: none") {
    SimpleTaskPtr task(TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB));
    JARVIS_ASSERT(task != nullptr);
    uint64_t va = 0x20000000;
    for (size_t i = 0; i < BufferPool::MAX_BUFFERS; ++i) {
        uint64_t h = BufferPool::alloc(*task, va + i * arch::PAGE_SIZE);
        if (h == 0) {
            JARVIS_TEST_PASS();
            return;
        }
    }
    uint64_t overflow = BufferPool::alloc(*task, va + BufferPool::MAX_BUFFERS * arch::PAGE_SIZE);
    JARVIS_ASSERT_EQ(0ULL, overflow);
    int32_t idx = task->buf_list_head;
    while (idx != -1) {
        int32_t next = BufferPool::entries[idx].list_next;
        uint32_t gen = BufferPool::entries[idx].generation;
        uint64_t h = (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(idx);
        BufferPool::free(*task, h);
        idx = next;
    }
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_deterministic_zero_copy_transfer,
            "PRE: none | POST: none") {
    auto *sender = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    auto *receiver = TaskControlBlock::create_user([]() {}, 5, 10, 32_KiB);
    JARVIS_ASSERT(sender != nullptr && receiver != nullptr);
    auto cleanup = ScopeGuard([&]() {
        sender->cleanup();
        delete sender;
        receiver->cleanup();
        delete receiver;
    });
    uint64_t va = 0x30000000;
    uint64_t handle = BufferPool::alloc(*sender, va);
    JARVIS_ASSERT(handle != 0);
    JARVIS_ASSERT(BufferPool::transfer(handle, *sender, *receiver));
    JARVIS_ASSERT(BufferPool::entries[handle & 0xFFFFFFFF].owner_task ==
                  static_cast<uint32_t>(receiver->id));
    uint64_t recv_va = 0x40000000;
    JARVIS_ASSERT(BufferPool::map(*receiver, handle, recv_va));
    JARVIS_ASSERT(BufferPool::free(*receiver, handle));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_deterministic_inline_payload,
            "PRE: none | POST: none") {
    Message msg{};
    msg.sender_id = 1;
    msg.type = 42;
    msg.priority = 5;
    JARVIS_ASSERT(msg.sender_id == 1);
    JARVIS_ASSERT(msg.type == 42);
    JARVIS_ASSERT(msg.priority == 5);
    JARVIS_ASSERT(msg.data_size == 0);
    JARVIS_ASSERT(msg.buf_handle == 0);
    static_assert(sizeof(msg.data) == CONFIG_IPC_MAX_MSG_SIZE,
                  "Message payload must be inline (no heap pointer)");
    for (size_t i = 0; i < IPC_MAX_MSG_SIZE; ++i)
        msg.data[i] = static_cast<uint8_t>(i & 0xFF);
    for (size_t i = 0; i < IPC_MAX_MSG_SIZE; ++i)
        JARVIS_ASSERT(msg.data[i] == static_cast<uint8_t>(i & 0xFF));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(buffer_pool_deterministic_pool_config_consistent,
            "PRE: none | POST: none") {
    JARVIS_ASSERT(BufferPool::POOL_PAGES == CONFIG_BUFFER_POOL_PAGES);
    JARVIS_ASSERT(BufferPool::MAX_BUFFERS > 0);
    JARVIS_ASSERT(BufferPool::BUFFER_SIZE == arch::PAGE_SIZE);
    JARVIS_TEST_PASS();
}

void register_buffer_pool_deterministic_tests() {
    Logger::info("Registering buffer pool deterministic tests");
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_preallocated_pool);
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_no_dynamic_alloc);
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_exhaustion_returns_zero);
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_zero_copy_transfer);
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_inline_payload);
    JARVIS_REGISTER_TEST(buffer_pool_deterministic_pool_config_consistent);
}
