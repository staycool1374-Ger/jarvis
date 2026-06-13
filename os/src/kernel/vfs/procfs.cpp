#include <kernel/vfs/procfs.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <string.hpp>
#include <utils.hpp>

namespace kernel {
namespace vfs {

// ── helpers ──
static void uint64_to_str(char* buffer, uint64_t n) {
    char tmp[32];
    int pos = 32;
    tmp[--pos] = '\0';
    if (n == 0) tmp[--pos] = '0';
    else while (n) { tmp[--pos] = '0' + (n % 10); n /= 10; }
    size_t i = 0;
    while (tmp[pos]) buffer[i++] = tmp[pos++];
    buffer[i] = '\0';
}

// ── meminfo vnode ──
struct MemInfoVnode {
    Vnode base;
    char content[256];
    uint64_t content_len;
};

static int64_t meminfo_read(Vnode& self, uint8_t* buf, uint64_t count, uint64_t offset) {
    auto* mi = static_cast<MemInfoVnode*>(self.private_data);
    if (!mi) return -1;
    if (offset >= mi->content_len) return 0;
    uint64_t avail = mi->content_len - offset;
    if (count > avail) count = avail;
    memcpy(buf, mi->content + offset, count);
    return static_cast<int64_t>(count);
}

static int64_t meminfo_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int meminfo_open(Vnode&, uint64_t) { return 0; }
static void meminfo_close(Vnode&) {}

static int64_t meminfo_lseek(Vnode& self, int64_t offset, int whence, uint64_t* out_pos) {
    auto* mi = static_cast<MemInfoVnode*>(self.private_data);
    if (!mi) return -1;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    case SEEK_END: new_pos = mi->content_len + static_cast<uint64_t>(offset); break;
    default: return -1;
    }
    if (new_pos > mi->content_len) new_pos = mi->content_len;
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}

static int meminfo_fstat(Vnode& self, VfsStat& st) {
    auto* mi = static_cast<MemInfoVnode*>(self.private_data);
    if (!mi) return -1;
    st.st_size = mi->content_len;
    st.st_mode = S_IFREG;
    return 0;
}

static int meminfo_ioctl(Vnode&, uint64_t, void*) { return -1; }
static int meminfo_readdir(Vnode&, uint64_t&, Dirent&) { return -1; }
static Vnode* meminfo_lookup(Vnode&, const char*) { return nullptr; }

static const VnodeOps meminfo_ops = {
    meminfo_read, meminfo_write, meminfo_open, meminfo_close,
    meminfo_lseek, meminfo_fstat, meminfo_ioctl, meminfo_readdir, meminfo_lookup,
};

static MemInfoVnode meminfo_vnode = {};

// ── /proc/self vnode ── (redirects to current pid's dir)
static int64_t self_read(Vnode&, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t self_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int self_open(Vnode&, uint64_t) { return 0; }
static void self_close(Vnode&) {}
static int64_t self_lseek(Vnode&, int64_t, int, uint64_t*) { return -1; }
static int self_fstat(Vnode&, VfsStat& vfs_stat) {
    vfs_stat.st_size = 0;
    vfs_stat.st_mode = S_IFDIR;
    return 0;
}
static int self_ioctl(Vnode&, uint64_t, void*) { return -1; }
static int self_readdir(Vnode&, uint64_t&, Dirent&) { return -1; }

static Vnode* self_lookup(Vnode& self, const char* name);

static const VnodeOps self_ops = {
    self_read, self_write, self_open, self_close,
    self_lseek, self_fstat, self_ioctl, self_readdir, self_lookup,
};

static Vnode self_vnode = {
    &self_ops, 1, 0, S_IFDIR, nullptr,
};

// ── pid stat vnode ──
struct PidStatVnode {
    Vnode base;
    uint64_t pid;
    TaskControlBlock* task;
};

static int64_t pid_stat_read(Vnode& self, uint8_t* buf, uint64_t count, uint64_t offset) {
    auto* ps = static_cast<PidStatVnode*>(self.private_data);
    if (!ps || !ps->task) return -1;

    char tmp[128];
    char id_str[32];
    char prio_str[32];
    uint64_to_str(id_str, ps->task->id);
    uint64_to_str(prio_str, ps->task->priority);

    size_t len = 0;
    const char* state_str = "?";
    switch (ps->task->state) {
    case TaskState::READY:     state_str = "R"; break;
    case TaskState::RUNNING:   state_str = "R"; break;
    case TaskState::BLOCKED:   state_str = "B"; break;
    case TaskState::WAITING:   state_str = "W"; break;
    case TaskState::TERMINATED: state_str = "T"; break;
    }

    const char* fmt = "pid=%s state=%s priority=%s ticks=%lu\n";
    const char* f = fmt;
    while (*f) tmp[len++] = *f++;
    const char* src = id_str; while (*src) tmp[len++] = *src++;
    f = fmt + 4; while (*f != '%') tmp[len++] = *f++;
    src = state_str; while (*src) tmp[len++] = *src++;
    while (*f != '%') tmp[len++] = *f++;
    src = prio_str; while (*src) tmp[len++] = *src++;
    while (*f) { if (*f == '\\' && *(f+1) == 'n') { tmp[len++] = '\n'; f += 2; break; } tmp[len++] = *f++; }
    src = fmt;
    while (*src != '%') src++;
    src++; // skip %
    uint64_to_str(id_str, ps->task->executed_ticks);
    src = id_str; while (*src) tmp[len++] = *src++;
    tmp[len] = '\0';

    if (offset >= len) return 0;
    uint64_t avail = len - offset;
    if (count > avail) count = avail;
    memcpy(buf, tmp + offset, count);
    return static_cast<int64_t>(count);
}

static int64_t pid_stat_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int pid_stat_open(Vnode&, uint64_t) { return 0; }
static void pid_stat_close(Vnode&) {}
static int64_t pid_stat_lseek(Vnode& self, int64_t offset, int whence, uint64_t* out_pos) {
    (void)self;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    default: new_pos = static_cast<uint64_t>(offset); break;
    }
    if (new_pos > 256) new_pos = 256;
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}
static int pid_stat_fstat(Vnode&, VfsStat&) { return -1; }
static int pid_stat_ioctl(Vnode&, uint64_t, void*) { return -1; }
static int pid_stat_readdir(Vnode&, uint64_t&, Dirent&) { return -1; }
static Vnode* pid_stat_lookup(Vnode&, const char*) { return nullptr; }

static const VnodeOps pid_stat_ops = {
    pid_stat_read, pid_stat_write, pid_stat_open, pid_stat_close,
    pid_stat_lseek, pid_stat_fstat, pid_stat_ioctl, pid_stat_readdir, pid_stat_lookup,
};

// ── pid directory vnode ──
struct PidDirVnode {
    Vnode base;
    uint64_t pid;
    TaskControlBlock* task;
    Vnode stat_vnode;
    PidStatVnode stat_data;
};

static int64_t pid_dir_read(Vnode&, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t pid_dir_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int pid_dir_open(Vnode&, uint64_t) { return 0; }
static void pid_dir_close(Vnode&) {}
static int64_t pid_dir_lseek(Vnode&, int64_t, int, uint64_t*) { return -1; }
static int pid_dir_fstat(Vnode&, VfsStat&) { return -1; }
static int pid_dir_ioctl(Vnode&, uint64_t, void*) { return -1; }
static int pid_dir_readdir(Vnode&, uint64_t&, Dirent&) { return -1; }

static Vnode* pid_dir_lookup(Vnode& self, const char* name) {
    auto* pd = static_cast<PidDirVnode*>(self.private_data);
    if (!pd) return nullptr;
    if (strcmp(name, "stat") == 0) return &pd->stat_vnode;
    return nullptr;
}

static const VnodeOps pid_dir_ops = {
    pid_dir_read, pid_dir_write, pid_dir_open, pid_dir_close,
    pid_dir_lseek, pid_dir_fstat, pid_dir_ioctl, pid_dir_readdir, pid_dir_lookup,
};

// ── self resolution ──
static Vnode* self_lookup(Vnode& self, const char* name) {
    (void)self;
    TaskControlBlock* task = Scheduler::current_task();
    if (!task) return nullptr;

    // Look for an existing pid dir or create one dynamically
    // For simplicity, return stat of current task
    if (strcmp(name, "stat") == 0) {
        static PidStatVnode cur_stat = {};
        cur_stat.pid = task->id;
        cur_stat.task = task;
        cur_stat.base.ops = &pid_stat_ops;
        cur_stat.base.ino = task->id;
        cur_stat.base.size = 0;
        cur_stat.base.mode = S_IFREG;
        cur_stat.base.private_data = &cur_stat;
        return &cur_stat.base;
    }
    return nullptr;
}

// ── procfs root ──
static int64_t proc_root_read(Vnode&, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t proc_root_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int proc_root_open(Vnode&, uint64_t) { return 0; }
static void proc_root_close(Vnode&) {}
static int64_t proc_root_lseek(Vnode&, int64_t, int, uint64_t*) { return -1; }
static int proc_root_fstat(Vnode&, VfsStat& vfs_stat) {
    vfs_stat.st_size = 0;
    vfs_stat.st_mode = S_IFDIR;
    return 0;
}
static int proc_root_ioctl(Vnode&, uint64_t, void*) { return -1; }

static int proc_root_readdir(Vnode& self, uint64_t& pos, Dirent& dent) {
    (void)self;
    uint64_t position = pos;
    // Static entries first
    if (position == 0) {
        const char* n = "meminfo";
        size_t i = 0; while (n[i] && i < 63) { dent.d_name[i] = n[i]; ++i; }
        dent.d_name[i] = '\0';
        dent.d_ino = 0;
        ++pos;
        return 0;
    }
    if (position == 1) {
        const char* n = "self";
        size_t i = 0; while (n[i] && i < 63) { dent.d_name[i] = n[i]; ++i; }
        dent.d_name[i] = '\0';
        dent.d_ino = 1;
        ++pos;
        return 0;
    }
    // PID entries
    // For readdir, skip position-2 entries worth of PIDs
    uint64_t task_count = Scheduler::task_count();
    uint64_t pid_idx = position - 2;
    uint64_t found = 0;
    for (uint64_t task_idx = 0; task_idx < task_count && found <= pid_idx; ++task_idx) {
        TaskControlBlock* t = Scheduler::task_at(task_idx);
        if (t) {
            if (found == pid_idx) {
                char pid_str[16];
                uint64_to_str(pid_str, t->id);
                size_t i = 0; while (pid_str[i] && i < 63) { dent.d_name[i] = pid_str[i]; ++i; }
                dent.d_name[i] = '\0';
                dent.d_ino = t->id;
                ++pos;
                return 0;
            }
            ++found;
        }
    }
    return -1;
}

static Vnode* proc_root_lookup(Vnode& self, const char* name) {
    (void)self;
    if (strcmp(name, "meminfo") == 0) return &meminfo_vnode.base;
    if (strcmp(name, "self") == 0) return &self_vnode;

    // Parse numeric PID
    uint64_t pid = 0;
    const char* pid_str = name;
    while (*pid_str >= '0' && *pid_str <= '9') {
        pid = pid * 10 + (*pid_str - '0');
        ++pid_str;
    }
    if (*pid_str == '\0' && pid > 0) {
        // Find task by PID
        uint64_t task_count = Scheduler::task_count();
        for (uint64_t task_idx = 0; task_idx < task_count; ++task_idx) {
            TaskControlBlock* t = Scheduler::task_at(task_idx);
            if (t && t->id == pid) {
                auto* pd = static_cast<PidDirVnode*>(
                    MemPool::alloc(sizeof(PidDirVnode)));
                if (!pd) return nullptr;
                pd->pid = pid;
                pd->task = t;
                pd->base.ops = &pid_dir_ops;
                pd->base.ino = pid;
                pd->base.size = 0;
                pd->base.mode = S_IFDIR;
                pd->base.private_data = pd;
                pd->stat_vnode.ops = &pid_stat_ops;
                pd->stat_vnode.ino = pid;
                pd->stat_vnode.size = 0;
                pd->stat_vnode.mode = S_IFREG;
                pd->stat_vnode.private_data = &pd->stat_data;
                pd->stat_data.pid = pid;
                pd->stat_data.task = t;
                return &pd->base;
            }
        }
    }
    return nullptr;
}

static const VnodeOps proc_root_ops = {
    proc_root_read, proc_root_write, proc_root_open, proc_root_close,
    proc_root_lseek, proc_root_fstat, proc_root_ioctl, proc_root_readdir, proc_root_lookup,
};

static Vnode proc_root_vnode = {
    &proc_root_ops, 0, 0, S_IFDIR, nullptr,
};

static Vnode* proc_get_root() {
    // Refresh meminfo content
    uint64_t total = PMM::total_memory();
    uint64_t free = PMM::free_memory();

    char tmp[256];
    size_t pos = 0;
    const char* s1 = "MemTotal: ";
    while (*s1) tmp[pos++] = *s1++;
    char num[32];
    uint64_to_str(num, total);
    char* np = num; while (*np) tmp[pos++] = *np++;
    const char* s2 = " kB\nMemFree: ";
    while (*s2) tmp[pos++] = *s2++;
    uint64_to_str(num, free);
    np = num; while (*np) tmp[pos++] = *np++;
    const char* s3 = " kB\n";
    while (*s3) tmp[pos++] = *s3++;
    tmp[pos] = '\0';

    memcpy(meminfo_vnode.content, tmp, pos);
    meminfo_vnode.content_len = pos;
    meminfo_vnode.base.ops = &meminfo_ops;
    meminfo_vnode.base.ino = 0;
    meminfo_vnode.base.size = pos;
    meminfo_vnode.base.mode = S_IFREG;
    meminfo_vnode.base.private_data = &meminfo_vnode;

    return &proc_root_vnode;
}

Filesystem proc_fs = {
    "procfs",
    proc_get_root,
};

} // namespace vfs
} // namespace kernel
