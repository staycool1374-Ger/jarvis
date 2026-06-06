#include <kernel/vfs/pipe.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <string.hpp>

namespace kernel {
namespace vfs {

static constexpr size_t PIPE_BUF_SIZE = 4096;

struct PipeBuffer {
    uint8_t data[PIPE_BUF_SIZE];
    size_t read_pos = 0;
    size_t write_pos = 0;
    size_t count = 0;
    int refcount = 2;
    bool read_closed = false;
    bool write_closed = false;
    TaskControlBlock* read_waiter = nullptr;
};

static int64_t pipe_read(Vnode* self, uint8_t* buf, uint64_t count, uint64_t) {
    auto* pb = static_cast<PipeBuffer*>(self->private_data);
    if (!pb || pb->read_closed) return -1;
    if (pb->write_closed && pb->count == 0) return 0;
    while (pb->count == 0) {
        pb->read_waiter = Scheduler::current_task();
        Scheduler::current_task()->state = TaskState::BLOCKED;
        Scheduler::reschedule();
        pb->read_waiter = nullptr;
        if (pb->write_closed && pb->count == 0) return 0;
        if (pb->read_closed) return -1;
    }
    uint64_t total = 0;
    while (total < count && pb->count > 0) {
        buf[total++] = pb->data[pb->read_pos];
        pb->read_pos = (pb->read_pos + 1) % PIPE_BUF_SIZE;
        --pb->count;
    }
    return static_cast<int64_t>(total);
}

static int64_t pipe_write(Vnode* self, const uint8_t* buf, uint64_t count, uint64_t) {
    auto* pb = static_cast<PipeBuffer*>(self->private_data);
    if (!pb || pb->read_closed) return -1;
    if (pb->write_closed) return -1;
    uint64_t total = 0;
    while (total < count) {
        if (pb->count >= PIPE_BUF_SIZE) {
            return static_cast<int64_t>(total);
        }
        pb->data[pb->write_pos] = buf[total++];
        pb->write_pos = (pb->write_pos + 1) % PIPE_BUF_SIZE;
        ++pb->count;
    }
    if (pb->read_waiter && pb->count > 0) {
        pb->read_waiter->state = TaskState::READY;
        pb->read_waiter = nullptr;
    }
    return static_cast<int64_t>(total);
}

static int pipe_open(Vnode*, uint64_t) { return 0; }

static void pipe_read_close(Vnode* self) {
    auto* pb = static_cast<PipeBuffer*>(self->private_data);
    if (!pb) return;
    pb->read_closed = true;
    --pb->refcount;
    if (pb->refcount <= 0) { delete pb; }
    self->private_data = nullptr;
    delete self;
}

static void pipe_write_close(Vnode* self) {
    auto* pb = static_cast<PipeBuffer*>(self->private_data);
    if (!pb) return;
    pb->write_closed = true;
    if (pb->read_waiter) {
        pb->read_waiter->state = TaskState::READY;
        pb->read_waiter = nullptr;
    }
    --pb->refcount;
    if (pb->refcount <= 0) { delete pb; }
    self->private_data = nullptr;
    delete self;
}

static int64_t pipe_lseek(Vnode*, int64_t, int, uint64_t*) { return -1; }
static int pipe_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFCHR;
    return 0;
}
static int pipe_ioctl(Vnode*, uint64_t, void*) { return -1; }
static int pipe_readdir(Vnode*, uint64_t*, Dirent*) { return -1; }
static Vnode* pipe_lookup(Vnode*, const char*) { return nullptr; }

static const VnodeOps pipe_read_ops = {
    pipe_read, nullptr, pipe_open, pipe_read_close,
    pipe_lseek, pipe_fstat, pipe_ioctl, pipe_readdir, pipe_lookup,
};

static const VnodeOps pipe_write_ops = {
    nullptr, pipe_write, pipe_open, pipe_write_close,
    pipe_lseek, pipe_fstat, pipe_ioctl, pipe_readdir, pipe_lookup,
};

int create_pipe(int fds[2]) {
    auto* pb = new PipeBuffer{};
    if (!pb) return -1;

    auto* rnode = new Vnode{};
    auto* wnode = new Vnode{};
    if (!rnode || !wnode) {
        delete pb; delete rnode; delete wnode;
        return -1;
    }

    rnode->ops = &pipe_read_ops;
    rnode->ino = 0;
    rnode->size = PIPE_BUF_SIZE;
    rnode->mode = S_IFCHR;
    rnode->private_data = pb;
    rnode->refcount = 1;

    wnode->ops = &pipe_write_ops;
    wnode->ino = 0;
    wnode->size = PIPE_BUF_SIZE;
    wnode->mode = S_IFCHR;
    wnode->private_data = pb;
    wnode->refcount = 1;

    auto* task = Scheduler::current_task();
    if (!task) { delete pb; delete rnode; delete wnode; return -1; }

    int rfd = task->fd_table.alloc();
    int wfd = task->fd_table.alloc();
    if (rfd < 0 || wfd < 0) {
        if (rfd >= 0) task->fd_table.free(rfd);
        if (wfd >= 0) task->fd_table.free(wfd);
        delete pb; delete rnode; delete wnode;
        return -1;
    }

    task->fd_table.fds[rfd].vnode = rnode;
    task->fd_table.fds[rfd].offset = 0;
    task->fd_table.fds[rfd].flags = 0;

    task->fd_table.fds[wfd].vnode = wnode;
    task->fd_table.fds[wfd].offset = 0;
    task->fd_table.fds[wfd].flags = 0;

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

} // namespace vfs
} // namespace kernel