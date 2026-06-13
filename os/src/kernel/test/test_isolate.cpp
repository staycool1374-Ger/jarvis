#include <kernel/test/test_isolate.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/daemon/daemon_mgr.hpp>

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

static size_t total_size() {
    return off_daemon_num() + sizeof(uint64_t);
}

bool snapshot_create() {
    size_t total = total_size();
    size_t pages = (total + arch::PAGE_SIZE - 1) / arch::PAGE_SIZE;
    uint64_t phys = PMM::alloc_contiguous(pages);
    if (!phys) return false;
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

    return true;
}

void snapshot_restore() {
    if (!g_snapshot) return;

    // ---- PMM (restore bitmap, then re-mark snapshot buffer pages) ----
    {
        uint64_t buf_phys = reinterpret_cast<uint64_t>(g_snapshot)
                            - arch::HHDM_OFFSET;
        size_t   buf_pages = (g_snapshot_size + arch::PAGE_SIZE - 1)
                             / arch::PAGE_SIZE;
        __builtin_memcpy(PMM::bitmap_ptr(),
                         g_snapshot + off_pmm_bitmap(), PMM::bitmap_bytes());
        __builtin_memcpy(PMM::owner_bitmap_ptr(),
                         g_snapshot + off_pmm_owner(),  PMM::bitmap_bytes());
        // Re-mark snapshot buffer pages so the next test cannot allocate
        // them and overwrite the saved state.  bitmap_set / owner_set
        // are private; write directly since we know the layout.
        {
            uint8_t* bmp = PMM::bitmap_ptr();
            uint8_t* own = PMM::owner_bitmap_ptr();
            for (size_t i = 0; i < buf_pages; ++i) {
                size_t idx = (buf_phys / arch::PAGE_SIZE) + i;
                bmp[idx / 8] |= static_cast<uint8_t>(1 << (idx % 8));
                own[idx / 8] &= static_cast<uint8_t>(~(1 << (idx % 8)));
            }
        }
        uint64_t saved_free = *reinterpret_cast<uint64_t*>(
            g_snapshot + off_pmm_free());
        PMM::free_pages_ref() = (saved_free > buf_pages)
                                ? saved_free - buf_pages : 0;
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

} // namespace kernel::test
