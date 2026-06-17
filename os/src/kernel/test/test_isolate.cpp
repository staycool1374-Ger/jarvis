#include <kernel/test/test_isolate.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/irq_guard.hpp>

namespace kernel::test {

static uint8_t* g_snapshot = nullptr;
static size_t   g_snapshot_size = 0;

// --- buffer layout helpers (all offsets relative to g_snapshot) ---
static size_t off_pmm_bitmap()   { return 0; }
static size_t off_pmm_owner()    { return off_pmm_bitmap() + PMM::bitmap_bytes(); }
static size_t off_pmm_free()     { return off_pmm_owner() + PMM::bitmap_bytes(); }

static size_t off_mempool_meta() { return off_pmm_free() + sizeof(uint64_t); }
static size_t off_mempool_data() { return off_mempool_meta()
                                       + MemPool::pool_count() * sizeof(MemPool::PoolMeta); }

static size_t off_sched_tasks()  { return off_mempool_data() + MemPool::pool_data_bytes(); }
static size_t off_sched_idtable(){ return off_sched_tasks()
                                       + Scheduler::snapshot_max_tasks() * sizeof(TaskControlBlock*); }
static size_t off_sched_misc()   { return off_sched_idtable()
                                       + Scheduler::snapshot_id_size() * sizeof(TaskControlBlock*); }

static size_t off_daemon_entries(){ return off_sched_misc()
                                       + sizeof(uint64_t) * 4  // task_count, cur_idx, next_id, idle_ptr
                                       + sizeof(bool); }       // preempt_enabled
static size_t off_daemon_num()    { return off_daemon_entries()
                                       + daemon::MAX_DAEMONS * sizeof(daemon::DaemonEntry); }

static size_t off_vfsd_pid()       { return off_daemon_num() + sizeof(uint64_t); }
static size_t off_iocd_pid()       { return off_vfsd_pid() + sizeof(uint64_t); }

static size_t off_bufpool()        { return off_iocd_pid() + sizeof(uint64_t); }

static size_t off_rsrc_counts()   { return off_bufpool() + BufferPool::state_bytes(); }
static size_t off_user_page_count(){ return off_rsrc_counts() + sizeof(ResourceCounters); }
static size_t off_user_page_data() { return off_user_page_count() + sizeof(uint64_t); }

static size_t total_size(size_t user_page_count) {
    return off_user_page_data()
         + user_page_count * (sizeof(uint64_t) + arch::PAGE_SIZE);
}

bool snapshot_create() {
    arch::IrqGuard guard;

    // Count user-owned pages from the owner bitmap
    size_t user_page_count = 0;
    {
        size_t total_pages_sys = PMM::total_memory() / arch::PAGE_SIZE;
        uint8_t* owner_bmp = PMM::owner_bitmap_ptr();
        for (size_t i = 0; i < total_pages_sys; ++i) {
            if ((owner_bmp[i / 8] >> (i % 8)) & 1)
                ++user_page_count;
        }
    }

    size_t total = total_size(user_page_count);
    size_t pages = (total + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    uint64_t phys = PMM::alloc_contiguous(pages);
    if (!phys) { return false; }
    g_snapshot = reinterpret_cast<uint8_t*>(phys + arch::HHDM_OFFSET);
    g_snapshot_size = total;

    // ---- PMM ----
    {
        __builtin_memcpy(g_snapshot + off_pmm_bitmap(), PMM::bitmap_ptr(),
                         PMM::bitmap_bytes());
        __builtin_memcpy(g_snapshot + off_pmm_owner(),  PMM::owner_bitmap_ptr(),
                         PMM::bitmap_bytes());
        *reinterpret_cast<uint64_t*>(g_snapshot + off_pmm_free()) =
            PMM::free_pages_ref();
    }

    // ---- MemPool ----
    {
        auto* meta = reinterpret_cast<MemPool::PoolMeta*>(
            g_snapshot + off_mempool_meta());
        for (size_t i = 0; i < MemPool::pool_count(); ++i)
            MemPool::capture_pool_meta(i, meta[i]);
        MemPool::capture_pool_data(g_snapshot + off_mempool_data());
    }

    // ---- Scheduler ----
    {
        auto* tasks   = reinterpret_cast<TaskControlBlock**>(
                            g_snapshot + off_sched_tasks());
        auto* idtable = reinterpret_cast<TaskControlBlock**>(
                            g_snapshot + off_sched_idtable());
        auto* misc    = reinterpret_cast<uint64_t*>(
                            g_snapshot + off_sched_misc());
        // misc[0]=task_count, misc[1]=current_idx, misc[2]=next_id
        // misc[3] stores idle ptr as bits
        // preempt is in byte off_sched_misc() + 4*8
        bool& preempt = *reinterpret_cast<bool*>(
                            g_snapshot + off_sched_misc() + sizeof(uint64_t) * 4);
        TaskControlBlock* idle_dummy = nullptr;
        Scheduler::capture_state(tasks, idtable,
                                 misc[0], misc[1], misc[2],
                                 idle_dummy, preempt);
        // Store the idle pointer (which capture_state set) as bits in misc[3].
        // Use memcpy to avoid strict-aliasing violations.
        __builtin_memcpy(&misc[3], &idle_dummy, sizeof(idle_dummy));
    }

    // ---- Daemon ----
    {
        auto* entries = reinterpret_cast<daemon::DaemonEntry*>(
                            g_snapshot + off_daemon_entries());
        uint64_t& num = *reinterpret_cast<uint64_t*>(
                            g_snapshot + off_daemon_num());
        daemon::capture_state(entries, num);
    }

    // ---- VFSD / IOCD PIDs (static globals not part of any snapshotted subsystem) ----
    *reinterpret_cast<uint64_t*>(g_snapshot + off_vfsd_pid()) = vfsd::get_vfsd_pid();
    *reinterpret_cast<uint64_t*>(g_snapshot + off_iocd_pid()) = iocd::get_iocd_pid();

    // ---- BufferPool ----
    BufferPool::capture_state(g_snapshot + off_bufpool(),
                              BufferPool::state_bytes());

    // ---- Resource Counters ----
    ResourceTracker::instance().capture(
        *reinterpret_cast<ResourceCounters*>(g_snapshot + off_rsrc_counts()));

    // ---- User page content ----
    {
        *reinterpret_cast<uint64_t*>(g_snapshot + off_user_page_count()) = user_page_count;
        uint8_t* out = g_snapshot + off_user_page_data();
        size_t total_pages_sys = PMM::total_memory() / arch::PAGE_SIZE;
        uint8_t* owner_bmp = PMM::owner_bitmap_ptr();
        for (size_t i = 0; i < total_pages_sys; ++i) {
            if ((owner_bmp[i / 8] >> (i % 8)) & 1) {
                *reinterpret_cast<uint64_t*>(out) = i;
                __builtin_memcpy(out + sizeof(uint64_t),
                                 reinterpret_cast<void*>(i * arch::PAGE_SIZE
                                                         + arch::HHDM_OFFSET),
                                 arch::PAGE_SIZE);
                out += sizeof(uint64_t) + arch::PAGE_SIZE;
            }
        }
    }

    return true;
}

void snapshot_restore(const char* test_name) {
    if (!g_snapshot) return;
    arch::IrqGuard guard;

    // Clear any pending context-switch state that a test may have left
    // via reschedule().  Without this, the next timer IRQ would use stale
    // RSP/CR3 values and corrupt memory.
    scheduler_save_rsp_to   = nullptr;
    scheduler_load_rsp_from  = 0;
    scheduler_load_cr3_from  = 0;
    scheduler_next_task_id   = 0;

    // Check resource counters before restoring — warn on any leaks / double-frees
    {
        ResourceCounters baseline;
        __builtin_memcpy(&baseline, g_snapshot + off_rsrc_counts(),
                         sizeof(baseline));
        ResourceTracker::instance().check(
            baseline,
            test_name ? test_name : "snapshot");
    }

    // ---- PMM ----
    {
        __builtin_memcpy(PMM::bitmap_ptr(),
                         g_snapshot + off_pmm_bitmap(), PMM::bitmap_bytes());
        __builtin_memcpy(PMM::owner_bitmap_ptr(),
                         g_snapshot + off_pmm_owner(),  PMM::bitmap_bytes());
        PMM::free_pages_ref() =
            *reinterpret_cast<uint64_t*>(g_snapshot + off_pmm_free());
    }

    // ---- User page content (restore after PMM bitmap so pages are marked allocated) ----
    {
        uint64_t saved_count = *reinterpret_cast<uint64_t*>(
                                   g_snapshot + off_user_page_count());
        uint8_t* in = g_snapshot + off_user_page_data();
        for (uint64_t p = 0; p < saved_count; ++p) {
            uint64_t page_index = *reinterpret_cast<uint64_t*>(in);
            __builtin_memcpy(
                reinterpret_cast<void*>(page_index * arch::PAGE_SIZE
                                        + arch::HHDM_OFFSET),
                in + sizeof(uint64_t),
                arch::PAGE_SIZE);
            in += sizeof(uint64_t) + arch::PAGE_SIZE;
        }
    }

    // ---- MemPool (restore data first, then meta rebuilds free list) ----
    {
        MemPool::restore_pool_data(g_snapshot + off_mempool_data());
        auto* meta = reinterpret_cast<const MemPool::PoolMeta*>(
                         g_snapshot + off_mempool_meta());
        for (size_t i = 0; i < MemPool::pool_count(); ++i)
            MemPool::restore_pool_meta(i, meta[i]);
    }

    // ---- Scheduler ----
    {
        auto* tasks    = reinterpret_cast<TaskControlBlock* const*>(
                             g_snapshot + off_sched_tasks());
        auto* idtable  = reinterpret_cast<TaskControlBlock* const*>(
                             g_snapshot + off_sched_idtable());
        auto* misc     = reinterpret_cast<uint64_t*>(
                             g_snapshot + off_sched_misc());
        bool preempt   = *reinterpret_cast<bool*>(
                             g_snapshot + off_sched_misc() + sizeof(uint64_t) * 4);
        TaskControlBlock* idle = nullptr;
        __builtin_memcpy(&idle, &misc[3], sizeof(idle));
        Scheduler::restore_state(tasks, idtable,
                                 misc[0], misc[1], misc[2],
                                 idle, preempt);
    }

    // ---- Daemon ----
    {
        auto* entries = reinterpret_cast<const daemon::DaemonEntry*>(
                            g_snapshot + off_daemon_entries());
        uint64_t num  = *reinterpret_cast<uint64_t*>(
                            g_snapshot + off_daemon_num());
        daemon::restore_state(entries, num);
    }

    // ---- VFSD / IOCD PIDs (static globals not part of any snapshotted subsystem) ----
    vfsd::set_vfsd_pid(*reinterpret_cast<uint64_t*>(g_snapshot + off_vfsd_pid()));
    iocd::set_iocd_pid(*reinterpret_cast<uint64_t*>(g_snapshot + off_iocd_pid()));

    // ---- BufferPool ----
    BufferPool::restore_state(g_snapshot + off_bufpool(),
                              BufferPool::state_bytes());

    // ---- Resource Counters: restore tracker to snapshot-time baseline ----
    ResourceCounters saved;
    __builtin_memcpy(&saved, g_snapshot + off_rsrc_counts(), sizeof(saved));
    ResourceTracker::instance().restore(saved);

}

void snapshot_destroy() {
    if (!g_snapshot) return;
    uint64_t phys = reinterpret_cast<uint64_t>(g_snapshot) - arch::HHDM_OFFSET;
    size_t pages = (g_snapshot_size + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    for (size_t i = 0; i < pages; ++i)
        PMM::free_page(phys + i * arch::PAGE_SIZE);
    g_snapshot = nullptr;
    g_snapshot_size = 0;
}

void reload_daemon_tasks() {
    // Terminate old daemon tasks so reap_orphans will clean them up
    for (uint64_t i = 0; i < daemon::MAX_DAEMONS; ++i) {
        const auto& entry = daemon::get_entry(i);
        if (entry.pid != 0) {
            auto* task = Scheduler::find_task(entry.pid);
            if (task) {
                task->state = TaskState::TERMINATED;
                task->exit_code = 0;
            }
            // Resets both the daemon entry's pid and the external PID var
            daemon::notify_death(entry.pid);
        }
    }
    // Terminate any remaining user tasks (test_fork, etc.) whose page
    // tables/code pages were corrupted by test activity.
    // Skip the current task (e.g. shell running cmd_selftest) to avoid suicide.
    auto* current = Scheduler::current_task();
    for (uint64_t i = 1; i < Scheduler::task_count(); ++i) {
        auto* t = Scheduler::task_at(i);
        if (t && t != current && t->page_table_) {
            t->state = TaskState::TERMINATED;
            t->exit_code = 0;
        }
    }
    Scheduler::reap_orphans();
    daemon::restart_stale_daemons();
}

} // namespace kernel::test
