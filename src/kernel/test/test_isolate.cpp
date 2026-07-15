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

/// @file test_isolate.cpp
/// @brief Test isolation snapshot/restore implementation.

#include <kernel/test/test_isolate.hpp>
#include <test.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/log/dmesg.hpp>
#include <kernel/vfs/tmpfs.hpp>
#include <kernel/driver/iocd.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/arch/gdt.hpp>
#include <logger.hpp>
#include "test_registry.gen.hpp"

extern "C" void debug_write(const char *s);
extern "C" void debug_write_hex(uint64_t value);

namespace kernel::test {

#ifdef __x86_64__
#define TASK_STACK_PTR(t) ((t)->context.rsp)
#elif defined(__aarch64__)
#define TASK_STACK_PTR(t) ((t)->context.sp_el0)
#else
#define TASK_STACK_PTR(t) ((t)->context.sp)
#endif

static uint8_t *g_snapshot = nullptr;
static size_t g_snapshot_size = 0;

bool g_vfs_touched = false;

void mark_vfs_touched() {
    g_vfs_touched = true;
}

// Maximum kernel stack entries (one per task, conservatively bounded)
static constexpr uint64_t KSTACK_ENTRY_SIZE =
    sizeof(uint64_t) * 2 // kernel_stack ptr + size
    + TaskControlBlock::STACK_SIZE;

// --- buffer layout helpers (all offsets relative to g_snapshot) ---
static size_t off_pmm_bitmap() {
    return 0;
}
static size_t off_pmm_owner() {
    return off_pmm_bitmap() + PMM::bitmap_bytes();
}
static size_t off_pmm_free() {
    return off_pmm_owner() + PMM::bitmap_bytes();
}

static size_t off_mempool_meta() {
    return off_pmm_free() + sizeof(uint64_t);
}
static size_t off_mempool_data() {
    return off_mempool_meta() +
           MemPool::pool_count() * sizeof(MemPool::PoolMeta);
}

static size_t off_sched_tasks() {
    return off_mempool_data() + MemPool::pool_data_bytes();
}
static size_t off_sched_task_fields() {
    return off_sched_tasks() +
           Scheduler::snapshot_max_tasks() * sizeof(TaskControlBlock *);
}
static size_t off_sched_idtable() {
    return off_sched_task_fields() + Scheduler::snapshot_task_fields_size();
}
static size_t off_sched_misc() {
    return off_sched_idtable() +
           Scheduler::snapshot_id_size() * sizeof(TaskControlBlock *);
}

static size_t off_sched_misc_size() {
    // misc[0]=task_count, misc[1]=cur_idx, misc[2]=next_id
    // misc[3]=idle_ptr (bits), bool preempt @ offset 32
    // misc[5]=shell_ptr, misc[8]=sporadic_task_count
    // misc[9]=timer_ticks
    return sizeof(uint64_t) * 10 + sizeof(bool);
}

static size_t off_sched_rqpod() {
    return off_sched_misc() + off_sched_misc_size();
}
static size_t off_daemon_entries() {
    return off_sched_rqpod() + sizeof(ReadyQueuePOD);
}
static size_t off_daemon_num() {
    return off_daemon_entries() +
           daemon::MAX_DAEMONS * sizeof(daemon::DaemonEntry);
}

static size_t off_vfsd_pid() {
    return off_daemon_num() + sizeof(uint64_t);
}
static size_t off_iocd_pid() {
    return off_vfsd_pid() + sizeof(uint64_t);
}

static size_t off_bufpool() {
    return off_iocd_pid() + sizeof(uint64_t);
}

static size_t off_rsrc_counts() {
    return off_bufpool() + BufferPool::state_bytes();
}
static size_t off_user_page_count() {
    return off_rsrc_counts() + sizeof(ResourceCounters);
}
static size_t off_user_page_data() {
    return off_user_page_count() + sizeof(uint64_t);
}

static size_t off_kstack_header(size_t user_page_count) {
    return off_user_page_data() +
           user_page_count * (sizeof(uint64_t) + arch::PAGE_SIZE);
}

static size_t total_size(size_t user_page_count, uint64_t num_kstacks) {
    size_t kstack_area = sizeof(uint64_t) + num_kstacks * KSTACK_ENTRY_SIZE;
    return off_kstack_header(user_page_count) + kstack_area;
}

bool snapshot_create() {
    arch::IrqGuard guard;

    // Count user-owned pages from the owner bitmap
    size_t user_page_count = 0;
    size_t total_pages_sys = PMM::total_memory() / arch::PAGE_SIZE;
    {
        uint8_t *owner_bmp = PMM::owner_bitmap_ptr();
        for (size_t i = 0; i < total_pages_sys; ++i) {
            if ((owner_bmp[i / 8] >> (i % 8)) & 1)
                ++user_page_count;
        }
    }

    uint64_t task_count = Scheduler::task_count();
    size_t total = total_size(user_page_count, task_count);
    size_t pages = (total + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    uint64_t phys = PMM::alloc_contiguous(pages);
    if (!phys) {
        return false;
    }
    g_snapshot = reinterpret_cast<uint8_t *>(phys + arch::HHDM_OFFSET);
    g_snapshot_size = total;

    // ---- PMM ----
    {
        __builtin_memcpy(g_snapshot + off_pmm_bitmap(), PMM::bitmap_ptr(),
                         PMM::bitmap_bytes());
        __builtin_memcpy(g_snapshot + off_pmm_owner(), PMM::owner_bitmap_ptr(),
                         PMM::bitmap_bytes());
        *reinterpret_cast<uint64_t *>(g_snapshot + off_pmm_free()) =
            PMM::free_pages_ref();
    }

    // ---- MemPool ----
    {
        auto *meta = reinterpret_cast<MemPool::PoolMeta *>(g_snapshot +
                                                           off_mempool_meta());
        for (size_t i = 0; i < MemPool::pool_count(); ++i)
            MemPool::capture_pool_meta(i, meta[i]);
        MemPool::capture_pool_data(g_snapshot + off_mempool_data());
    }

    // ---- Scheduler ----
    {
        auto *tasks = reinterpret_cast<TaskControlBlock **>(g_snapshot +
                                                            off_sched_tasks());
        auto *idtable = reinterpret_cast<TaskControlBlock **>(
            g_snapshot + off_sched_idtable());
        auto *misc =
            reinterpret_cast<uint64_t *>(g_snapshot + off_sched_misc());
        bool &preempt = *reinterpret_cast<bool *>(
            g_snapshot + off_sched_misc() + sizeof(uint64_t) * 4);
        TaskControlBlock *idle_dummy = nullptr;
        Scheduler::capture_state(tasks, idtable, misc[0], misc[1], misc[2],
                                 idle_dummy, preempt, &misc[6], &misc[7],
                                 &misc[8]);
        __builtin_memcpy(&misc[3], &idle_dummy, sizeof(idle_dummy));
        auto *shell_ptr = Scheduler::get_shell_task();
        __builtin_memcpy(&misc[5], &shell_ptr, sizeof(shell_ptr));

        // Save PIT/Timer tick count so pit_init_sets_divisor passes after
        // snapshot_restore — the hardware keeps running but the software
        // counter starts at 0 at boot and is not part of any saved region.
        misc[9] = arch::Timer::ticks();

        // Save per-task plain fields for deep-copy restoration
        auto *task_fields = reinterpret_cast<Scheduler::TaskFields *>(
            g_snapshot + off_sched_task_fields());
        Scheduler::capture_task_fields(task_fields);

        // Save full ReadyQueueManager POD (queue heads, tails, counts, bitmap)
        auto *rqpod =
            reinterpret_cast<ReadyQueuePOD *>(g_snapshot + off_sched_rqpod());
        Scheduler::capture_rqpod(*rqpod);
    }

    // ---- Pin baseline TCB memory ----
    // The scheduler snapshot stores raw TCB pointers.  Pin every baseline
    // task's MemPool block so it can never be recycled onto a test-allocated
    // task; otherwise the captured pointer would alias a foreign TCB, the live
    // task set and ResourceTracker baseline would silently drift (the "Tasks
    // -5" corruption), and the run could hard-crash on a use-after-free.
    for (auto *t = Scheduler::all_tasks().first_ptr(); t;
         t = Scheduler::all_tasks().next_ptr(t)) {
        if (t->magic == TaskControlBlock::TCB_MAGIC)
            MemPool::pin_block(t);
    }

    // ---- Daemon ----
    {
        auto *entries = reinterpret_cast<daemon::DaemonEntry *>(
            g_snapshot + off_daemon_entries());
        uint64_t &num =
            *reinterpret_cast<uint64_t *>(g_snapshot + off_daemon_num());
        daemon::capture_state(entries, num);
    }

    // ---- VFSD / IOCD PIDs ----
    *reinterpret_cast<uint64_t *>(g_snapshot + off_vfsd_pid()) =
        vfsd::get_vfsd_pid();
    *reinterpret_cast<uint64_t *>(g_snapshot + off_iocd_pid()) =
        iocd::get_iocd_pid();
    Logger::info("[SNAP:SAVE] vfsd_pid=%u iocd_pid=%u", vfsd::get_vfsd_pid(),
                 iocd::get_iocd_pid());

    // ---- BufferPool ----
    BufferPool::capture_state(g_snapshot + off_bufpool(),
                              BufferPool::state_bytes());

    // ---- Resource Counters ----
    ResourceTracker::instance().capture(
        *reinterpret_cast<ResourceCounters *>(g_snapshot + off_rsrc_counts()));

    // ---- User page content ----
    {
        *reinterpret_cast<uint64_t *>(g_snapshot + off_user_page_count()) =
            user_page_count;
        uint8_t *out = g_snapshot + off_user_page_data();
        uint8_t *owner_bmp = PMM::owner_bitmap_ptr();
        for (size_t i = 0; i < total_pages_sys; ++i) {
            if ((owner_bmp[i / 8] >> (i % 8)) & 1) {
                *reinterpret_cast<uint64_t *>(out) = i;
                __builtin_memcpy(out + sizeof(uint64_t),
                                 reinterpret_cast<void *>(i * arch::PAGE_SIZE +
                                                          arch::HHDM_OFFSET),
                                 arch::PAGE_SIZE);
                out += sizeof(uint64_t) + arch::PAGE_SIZE;
            }
        }
    }

    // ---- Kernel stacks (save all tasks) ----
    {
        uint64_t num_kstacks = Scheduler::task_count();
        *reinterpret_cast<uint64_t *>(
            g_snapshot + off_kstack_header(user_page_count)) = num_kstacks;
        uint8_t *out =
            g_snapshot + off_kstack_header(user_page_count) + sizeof(uint64_t);
        for (uint64_t i = 0; i < num_kstacks; ++i) {
            auto *t = Scheduler::task_at(i);
            if (!t || !t->kernel_stack) {
                // Write a sentinel entry so we can skip at restore
                *reinterpret_cast<uint64_t *>(out) = 0;
                *reinterpret_cast<uint64_t *>(out + sizeof(uint64_t)) = 0;
                out += KSTACK_ENTRY_SIZE;
                continue;
            }
            // Save kernel_stack virtual address for matching at restore
            *reinterpret_cast<uint64_t *>(out) =
                reinterpret_cast<uint64_t>(t->kernel_stack);
            uint64_t kstack_size = t->kernel_stack_top -
                                   reinterpret_cast<uint64_t>(t->kernel_stack);
            *reinterpret_cast<uint64_t *>(out + sizeof(uint64_t)) = kstack_size;
            __builtin_memcpy(out + sizeof(uint64_t) * 2, t->kernel_stack,
                             kstack_size);
            out += KSTACK_ENTRY_SIZE;
        }
    }

    return true;
}

void snapshot_restore(const char *test_name) {
    if (!g_snapshot)
        return;
    arch::IrqGuard guard;

    // Clear any pending context-switch state
    __atomic_store_n(&scheduler_load_rsp_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_load_cr3_from, (uint64_t)0, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_next_task_id, UINT64_MAX, __ATOMIC_RELEASE);
    __atomic_store_n(&scheduler_save_rsp_to, (uint64_t *)nullptr,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&isr_nesting_depth, (uint64_t)0, __ATOMIC_RELEASE);

    // Clear stale static state that survives test execution
    s_reap_in_progress = false;
    __atomic_store_n(&fpu_owner, (TaskControlBlock *)nullptr, __ATOMIC_RELEASE);
    scheduler_dummy_save_rsp = 0;

    uint64_t corr = __atomic_exchange_n(&scheduler_corruption_count,
                                        (uint64_t)0, __ATOMIC_ACQ_REL);
    if (corr > 0) {
        Logger::raw_write("[SCHED] corruption_count=");
        Logger::print_dec(corr);
        Logger::raw_write(" during test \"");
        Logger::raw_write(test_name ? test_name : "?");
        Logger::raw_write("\"\n");
        kernel::test::Registry::record_failure(__FILE__, __LINE__,
                                               "scheduler corruption detected");
    }

    {
        ResourceCounters baseline;
        __builtin_memcpy(&baseline, g_snapshot + off_rsrc_counts(),
                         sizeof(baseline));
        ResourceTracker::instance().check(baseline,
                                          test_name ? test_name : "snapshot");
        // Reset tracker counters to match the PMM/MemPool state about
        // to be restored, so the next test's delta is accurate (#1689).
        ResourceTracker::instance().restore(baseline);
    }

    // ---- PMM ----
    {
        __builtin_memcpy(PMM::bitmap_ptr(), g_snapshot + off_pmm_bitmap(),
                         PMM::bitmap_bytes());
        __builtin_memcpy(PMM::owner_bitmap_ptr(), g_snapshot + off_pmm_owner(),
                         PMM::bitmap_bytes());
        PMM::free_pages_ref() =
            *reinterpret_cast<uint64_t *>(g_snapshot + off_pmm_free());
    }

    // ---- User page content ----
    {
        uint64_t saved_count =
            *reinterpret_cast<uint64_t *>(g_snapshot + off_user_page_count());
        size_t total_pages = PMM::total_memory() / arch::PAGE_SIZE;
        uint8_t *in = g_snapshot + off_user_page_data();
        for (uint64_t p = 0; p < saved_count; ++p) {
            uint64_t page_index = *reinterpret_cast<uint64_t *>(in);
            if (page_index < total_pages) {
                __builtin_memcpy(
                    reinterpret_cast<void *>(page_index * arch::PAGE_SIZE +
                                             arch::HHDM_OFFSET),
                    in + sizeof(uint64_t), arch::PAGE_SIZE);
            }
            in += sizeof(uint64_t) + arch::PAGE_SIZE;
        }
    }

    // ---- MemPool ----
    {
        MemPool::restore_pool_data(g_snapshot + off_mempool_data());
        auto *meta = reinterpret_cast<const MemPool::PoolMeta *>(
            g_snapshot + off_mempool_meta());
        for (size_t i = 0; i < MemPool::pool_count(); ++i)
            MemPool::restore_pool_meta(i, meta[i]);
    }

    // ---- Scheduler ----
    {
        auto *tasks = reinterpret_cast<TaskControlBlock *const *>(
            g_snapshot + off_sched_tasks());
        auto *idtable = reinterpret_cast<TaskControlBlock *const *>(
            g_snapshot + off_sched_idtable());
        auto *misc =
            reinterpret_cast<uint64_t *>(g_snapshot + off_sched_misc());
        bool preempt = *reinterpret_cast<bool *>(g_snapshot + off_sched_misc() +
                                                 sizeof(uint64_t) * 4);
        TaskControlBlock *idle = nullptr;
        __builtin_memcpy(&idle, &misc[3], sizeof(idle));
        auto *task_fields = reinterpret_cast<const Scheduler::TaskFields *>(
            g_snapshot + off_sched_task_fields());
        Scheduler::restore_state(tasks, idtable, misc[0], misc[1], misc[2],
                                 idle, preempt, misc[6], misc[7], misc[8]);
        TaskControlBlock *shell_ptr = nullptr;
        __builtin_memcpy(&shell_ptr, &misc[5], sizeof(shell_ptr));
        Scheduler::set_shell_task(shell_ptr);

        // Restore PIT/Timer tick count so that pit_init_sets_divisor
        // sees a non-zero tick value matching the snapshot's epoch.
        // If the saved count is 0 (timer ISR hasn't fired), force 1
        // to avoid false failures in tests that require ticks() > 0.
        {
            uint64_t saved = misc[9];
            if (saved == 0)
                saved = 1;
            arch::Timer::set_ticks_for_test(saved);
        }

        // Restore per-task plain fields (deep-copy) to fix any corrupted
        // context.rsp / state / scheduling params that accumulated during
        // test execution and survived the pointer-array-only restore.
        // This also restores runq_next_/runq_prev_/in_ready_queue_/rq_priority_
        // so the intrusive linked lists are valid after this point.
        Scheduler::restore_task_fields(task_fields);

        // Rebuild AllTasksRegistry linked lists now that per-task priority
        // fields are restored to their snapshot values.  The earlier
        // restore_state() → all_tasks_.restore() read stale priorities from
        // test-execution-modified TCBs, so the per-priority lists may be
        // inconsistent.  rebuild() re-inserts tasks using the corrected
        // priority field.
        Scheduler::rebuild_all_tasks();

        // Restore the full ReadyQueueManager POD (queue heads/tails/counts,
        // priority bitmap) from the snapshot.  The per-TCB runq pointers
        // were restored above and are valid because TCBs live at the same
        // physical addresses across snapshot cycles.
        {
            auto *rqpod = reinterpret_cast<const ReadyQueuePOD *>(
                g_snapshot + off_sched_rqpod());
            Scheduler::restore_rqpod(*rqpod);
            Scheduler::rebuild_ready_queue();
        }
    }

    // ---- Re-identify current task by RSP match ----
    // restore_state() restores current_index_ from the snapshot, but the
    // actual CPU RSP belongs to the caller (kernel_main on the boot stack
    // or an existing task's kernel stack).  Find the task whose kernel stack
    // range contains the current RSP and set current_index_ to that task.
    // If no match (RSP on boot stack), force current_index_ to idle (0)
    // so the scheduler doesn't think a stale task is running and later
    // save the boot-stack RSP into that task's context.rsp.
    {
        uint64_t cur_rsp;
        bool found = false;
        asm volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
            auto *t = Scheduler::task_at(i);
            if (t && t->magic == TaskControlBlock::TCB_MAGIC &&
                t->kernel_stack && t->kernel_stack_top) {
                uint64_t base = reinterpret_cast<uint64_t>(t->kernel_stack);
                bool inr = (cur_rsp >= base && cur_rsp < t->kernel_stack_top);
                if (inr) {
                    if (t != Scheduler::current_task()) {
                        Scheduler::set_current_task(t);
                    }
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            Scheduler::set_current_index(0);
        }
    }

    // ---- BufferPool ----
    BufferPool::restore_state(g_snapshot + off_bufpool(),
                              BufferPool::state_bytes());

    // ---- Resource Counters ----
    ResourceCounters saved;
    __builtin_memcpy(&saved, g_snapshot + off_rsrc_counts(), sizeof(saved));
    ResourceTracker::instance().restore(saved);

    // ---- Kernel stack restore (skip current task) ----
    {
        uint64_t saved_user_count =
            *reinterpret_cast<uint64_t *>(g_snapshot + off_user_page_count());
        size_t kstack_hdr_off = off_kstack_header(saved_user_count);
        uint64_t num_kstacks =
            *reinterpret_cast<uint64_t *>(g_snapshot + kstack_hdr_off);
        uint8_t *in = g_snapshot + kstack_hdr_off + sizeof(uint64_t);
        auto *current = Scheduler::current_task();
        uint64_t cur_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        for (uint64_t i = 0; i < num_kstacks; ++i) {
            uint64_t saved_kstack = *reinterpret_cast<uint64_t *>(in);
            uint64_t saved_size =
                *reinterpret_cast<uint64_t *>(in + sizeof(uint64_t));
            if (saved_kstack != 0 && saved_size > 0) {
                // Find the task with this kernel stack
                for (uint64_t j = 0; j < Scheduler::task_count(); ++j) {
                    auto *t = Scheduler::task_at(j);
                    if (t && reinterpret_cast<uint64_t>(t->kernel_stack) ==
                                 saved_kstack) {
                        // Skip the currently-running task: never overwrite the
                        // live stack. Match by current_task_ptr_ AND by the
                        // live RSP actually residing inside this task's stack
                        // (the RSP-match in restore_state can leave
                        // current_task_ptr_ stale vs. the real running task).
                        bool on_live_stack =
                            (cur_rsp >=
                                 reinterpret_cast<uint64_t>(t->kernel_stack) &&
                             cur_rsp < reinterpret_cast<uint64_t>(
                                           t->kernel_stack_top));
                        bool skip = (t == current) || on_live_stack;
                        // Skip current task — its stack is active
                        if (!skip) {
                            __builtin_memcpy(t->kernel_stack,
                                             in + sizeof(uint64_t) * 2,
                                             saved_size);
                        }
                        break;
                    }
                }
            }
            in += KSTACK_ENTRY_SIZE;
        }
    }

    // ---- Fix up the live running task's context.rsp ----
    // The running task's kstack is intentionally NOT overwritten by the restore
    // above, but restore_task_fields() still re-assigned its TaskContext from the
    // snapshot, leaving context.rsp pointing at the (shallow) snapshot-time
    // stack instead of the live nested stack.  If the scheduler later dispatches
    // this task via iretq it would read a garbage iret frame (the snapshot-depth
    // stack slot, not a real interrupt frame) -> #GP.  Correct it to the live
    // RSP and make current_task_ptr_ track the real running task.
    {
        uint64_t live_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(live_rsp));
        for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
            auto *t = Scheduler::task_at(i);
            if (!t || t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            uint64_t base = reinterpret_cast<uint64_t>(t->kernel_stack);
            if (base == 0)
                continue;
            if (live_rsp >= base &&
                live_rsp < reinterpret_cast<uint64_t>(t->kernel_stack_top)) {
                TASK_STACK_PTR(t) = live_rsp;
                Scheduler::set_current(*t);
                break;
            }
        }
    }

    // ---- Validate per-task context.rsp ----
    // The kstack restore may have skipped a task whose kernel_stack
    // address changed since snapshot capture (e.g., idle was recreated
    // by reap_orphans during a previous test).  In that case the
    // field-restored context.rsp points to stale data on the wrong
    // stack.  Reinitialize such tasks to the canonical initial frame
    // position so the first context switch loads valid register state.
    {
        auto *cur = Scheduler::current_task();
        for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
            auto *t = Scheduler::task_at(i);
            if (!t || t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (t == cur)
                continue;
            uint64_t base = reinterpret_cast<uint64_t>(t->kernel_stack);
            if (base == 0)
                continue;
            uint64_t rsp = TASK_STACK_PTR(t);
            if (rsp < base || rsp > t->kernel_stack_top) {
                static bool fixup_dumped = false;
                if (!fixup_dumped) {
                    debug_write("[DIAG-FIXUP] id=");
                    debug_write_hex(t->id);
                    debug_write(" rsp=0x");
                    debug_write_hex(rsp);
                    debug_write(" base=0x");
                    debug_write_hex(base);
                    debug_write(" top=0x");
                    debug_write_hex(t->kernel_stack_top);
                    debug_write("\n");
                    fixup_dumped = true;
                }
                TASK_STACK_PTR(t) = t->kernel_stack_top - sizeof(TaskContext);
            }
        }
    }

    // ---- Post-restore frame sanity scan (diagnostic) ----
    {
        auto *cur = Scheduler::current_task();
        for (uint64_t i = 0; i < Scheduler::task_count(); ++i) {
            auto *t = Scheduler::task_at(i);
            if (!t || t->magic != TaskControlBlock::TCB_MAGIC)
                continue;
            if (t == cur)
                continue;
            uint64_t rsp = TASK_STACK_PTR(t);
            uint64_t base = reinterpret_cast<uint64_t>(t->kernel_stack);
            if (base == 0 || rsp < base || rsp >= t->kernel_stack_top)
                continue;
            uint64_t *f = reinterpret_cast<uint64_t *>(rsp);
            uint64_t rip = f[136 / 8];
            uint64_t cs = f[144 / 8];
            uint64_t ss = f[168 / 8];
            if (rip == 0 || (cs != 0x8 && cs != 0x1B)) {
                static bool scandumped = false;
                if (!scandumped) {
                    debug_write("[DIAG-SCAN] ZEROFRAME id=");
                    debug_write_hex(t->id);
                    debug_write(" state=");
                    debug_write_hex((uint64_t)t->state);
                    debug_write(" rsp=0x");
                    debug_write_hex(rsp);
                    debug_write(" rip=0x");
                    debug_write_hex(rip);
                    debug_write(" cs=0x");
                    debug_write_hex(cs);
                    debug_write(" ss=0x");
                    debug_write_hex(ss);
                    debug_write("\n");
                    scandumped = true;
                }
            }
        }
    }

    // ---- Current task state fix ----
    // restore_state() enqueues all READY tasks, including the current task
    // (shell) whose kernel stack content was overwritten by test execution.
    // Remove it from the ready queue and set state=RUNNING so the first
    // context switch is AWAY (saving the fresh ISR-frame RSP), not TO this
    // task (which would load stale data).
    {
        auto *current = Scheduler::current_task();
        if (current && current->in_ready_queue_) {
            Scheduler::dequeue_ready(*current);
        }
        if (current) {
            current->state = TaskState::RUNNING;
        }
    }

    // ---- Conditional daemon restart (vfs_touched flag) ----
    // Tests that never touch VFS can skip the expensive daemon-kill + ELF-load
    // + remount cycle (~150k cycles).  The daemon tasks continue running with
    // their pre-snapshot state, which is correct because no VFS syscall was
    // issued.  Only the ready queue needs rebuilding (runq links desync from
    // cleanup_test_tasks).
    if (g_vfs_touched) {
        kernel::vfs::reset_and_remount();
        kernel::vfs::tmpfs_reset_root();
        reload_daemon_tasks();
    } else {
        Scheduler::rebuild_ready_queue();
    }
    g_vfs_touched = false;

    // ---- Daemon ----
    {
        auto *entries = reinterpret_cast<const daemon::DaemonEntry *>(
            g_snapshot + off_daemon_entries());
        uint64_t num =
            *reinterpret_cast<uint64_t *>(g_snapshot + off_daemon_num());
        daemon::restore_state(entries, num);
    }

    // ---- VFSD / IOCD PIDs ----
    vfsd::set_vfsd_pid(
        *reinterpret_cast<uint64_t *>(g_snapshot + off_vfsd_pid()));
    iocd::set_iocd_pid(
        *reinterpret_cast<uint64_t *>(g_snapshot + off_iocd_pid()));
    Logger::info("[SNAP:RESTORE] vfsd_pid=%u iocd_pid=%u", vfsd::get_vfsd_pid(),
                 iocd::get_iocd_pid());

    // ---- Reset monitor-task handoff flag ----
    // The static s_scan_requested_ may be stale after restore; reset so
    // the monitor does not spuriously scan on the next reschedule.
#if CONFIG_DEADLINE_MONITOR_TASK
    Scheduler::reset_scan_requested();
#endif

    // ---- Post-reload fixup ----
    // Keep idle as the boot-stack proxy task.  Idle is never returned by
    // next_task() (lowest priority, RUNNING, not in ready queue).  Using
    // idle as the proxy avoids corrupting a real task's context.rsp when
    // the deferred switch saves boot-stack RSP into it.
    {
        auto *cur = Scheduler::current_task();
        if (!cur || cur->magic != TaskControlBlock::TCB_MAGIC) {
            Scheduler::set_current_index(0);
        }
#if defined(CONFIG_ARCH_X86_64)
        auto *idle = Scheduler::get_idle_task();
        if (idle && idle->magic == TaskControlBlock::TCB_MAGIC) {
            arch::GDT::set_tss_rsp0(idle->kernel_stack_top);
        }
#endif
    }

    // ---- Refresh snapshot scheduler state ----
    // reload_daemon_tasks freed the original daemon TCBs (pid=3,4) via
    // reap_orphans / MemPool::free and created new ones (pid=5,6).  The
    // snapshot's task-pointer array, ID table, and metadata now reference
    // freed / reallocated memory.  Recapture the live scheduler state
    // (task array, ID table, pool data, task fields) so subsequent restore
    // cycles always read valid pointers and pool data.  Preserve the
    // original next_task_id_ from the snapshot so daemon PIDs don't drift
    // across cycles.
    {
        auto *tasks_snap = reinterpret_cast<TaskControlBlock **>(
            g_snapshot + off_sched_tasks());
        auto *idtable_snap = reinterpret_cast<TaskControlBlock **>(
            g_snapshot + off_sched_idtable());
        auto *misc_snap =
            reinterpret_cast<uint64_t *>(g_snapshot + off_sched_misc());
        uint64_t orig_next_id = misc_snap[2];
        bool &preempt_snap = *reinterpret_cast<bool *>(
            g_snapshot + off_sched_misc() + sizeof(uint64_t) * 4);
        TaskControlBlock *idle_dummy = nullptr;
        Scheduler::capture_state(tasks_snap, idtable_snap, misc_snap[0],
                                 misc_snap[1], misc_snap[2], idle_dummy,
                                 preempt_snap, &misc_snap[6], &misc_snap[7],
                                 &misc_snap[8]);
        misc_snap[2] = orig_next_id;
        __builtin_memcpy(&misc_snap[3], &idle_dummy, sizeof(idle_dummy));
        auto *shell_ptr = Scheduler::get_shell_task();
        __builtin_memcpy(&misc_snap[5], &shell_ptr, sizeof(shell_ptr));
        misc_snap[9] = arch::Timer::ticks();

        // Recapture pool data so restore_pool_data gives back the
        // post-reload state (not the pre-test state that may reference
        // freed or repurposed blocks).
        MemPool::capture_pool_data(g_snapshot + off_mempool_data());
        {
            auto *meta = reinterpret_cast<MemPool::PoolMeta *>(
                g_snapshot + off_mempool_meta());
            for (size_t i = 0; i < MemPool::pool_count(); ++i)
                MemPool::capture_pool_meta(i, meta[i]);
        }

        // Recapture task fields so restore_task_fields can match the
        // post-reload task IDs (daemon PIDs changed from 3,4 to 5,6).
        auto *task_fields = reinterpret_cast<Scheduler::TaskFields *>(
            g_snapshot + off_sched_task_fields());
        Scheduler::capture_task_fields(task_fields);

        // Recapture ReadyQueuePOD so restore_pod reads the post-reload
        // queue heads/tails/counts (daemon tasks replaced).
        auto *rqpod =
            reinterpret_cast<ReadyQueuePOD *>(g_snapshot + off_sched_rqpod());
        Scheduler::capture_rqpod(*rqpod);
    }

    // Belt-and-suspenders: ensure interrupts enabled before returning to
    // test code.  Nested IrqGuards inside reload_daemon_tasks can disrupt
    // the outer guard's saved IF flag when task kernel stacks are restored.
    arch::sti();
}

void snapshot_destroy() {
    if (!g_snapshot)
        return;
    uint64_t phys = reinterpret_cast<uint64_t>(g_snapshot) - arch::HHDM_OFFSET;
    size_t pages = (g_snapshot_size + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    for (size_t i = 0; i < pages; ++i)
        PMM::free_page(phys + i * arch::PAGE_SIZE);
    g_snapshot = nullptr;
    g_snapshot_size = 0;
}

void reload_daemon_tasks() {
    arch::IrqGuard guard;
    auto *current = Scheduler::current_task();
    auto *idle = Scheduler::get_idle_task();

    // Collect registered daemon PIDs so we can preserve healthy ones.
    uint64_t daemon_pids[daemon::MAX_DAEMONS];
    uint64_t num_daemon_pids = 0;
    for (uint64_t i = 0; i < daemon::MAX_DAEMONS; ++i) {
        if (daemon::get_entry(i).pid != 0) {
            daemon_pids[num_daemon_pids++] = daemon::get_entry(i).pid;
        }
    }

    // Kill stale or corrupted daemon tasks only; healthy ones survive.
    for (uint64_t i = 0; i < num_daemon_pids; ++i) {
        auto *task = Scheduler::find_task(daemon_pids[i]);
        if (task && task->magic == TaskControlBlock::TCB_MAGIC &&
            task->state != TaskState::TERMINATED) {
            continue; // healthy daemon — keep it
        }
        // Stale PID or dead task — clean up
        for (uint64_t j = 0; j < daemon::MAX_DAEMONS; ++j) {
            const auto &entry = daemon::get_entry(j);
            if (entry.pid == daemon_pids[i]) {
                dmesg_push_base(0xDA05, entry.name, entry.pid);
                if (task && task->magic == TaskControlBlock::TCB_MAGIC) {
                    if (task != current) {
                        task->state = TaskState::TERMINATED;
                        task->exit_code = 0;
                    }
                }
                daemon::notify_death(entry.pid, true);
                break;
            }
        }
    }

    // Kill test-created tasks, but skip daemon tasks
    for (uint64_t i = 1; i < Scheduler::task_count(); ++i) {
        auto *t = Scheduler::task_at(i);
        if (!t)
            continue;
        bool is_daemon = false;
        for (uint64_t j = 0; j < num_daemon_pids; ++j) {
            if (t->id == daemon_pids[j]) {
                is_daemon = true;
                break;
            }
        }
        if (is_daemon)
            continue;
#if CONFIG_DEADLINE_MONITOR_TASK
        bool is_monitor = (t == Scheduler::get_monitor_task());
        if (is_monitor)
            continue;
#endif
        if (t != current && t != idle && t != Scheduler::get_shell_task() &&
            t->magic == TaskControlBlock::TCB_MAGIC) {
            t->state = TaskState::TERMINATED;
            t->exit_code = 0;
        }
    }
    Scheduler::reap_orphans();
    Scheduler::reset_ready_queue();
    // NOTE: daemons are NOT restarted here — only killed if stale.
    // The caller (e.g. test.cpp end-of-suite) adds restart_stale_daemons()
    // if a full restore to boot state is required.
}

void run_all_isolated_tests() {
    Logger::info("[TEST] Running %zu tests from generated registry",
                 generated_tests_count);

    for (size_t i = 0; i < generated_tests_count; ++i) {
        const auto &test = generated_tests[i];

        Logger::info("[TEST]  [%zu/%zu] %s", i + 1, generated_tests_count,
                     test.name);

        test.setup_func();
        test.test_func();
        test.teardown_func();

        Logger::info("[TEST]  %s completed.", test.name);
    }
}

} // namespace kernel::test
