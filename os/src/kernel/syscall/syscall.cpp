#include <kernel/syscall/syscall.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/pipe.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <services/terminal/terminal.hpp>
#include <string.hpp>

namespace kernel {

static constexpr uint64_t MAX_PATH = 256;

void Syscall::init() {
}

static TaskControlBlock* task() {
    return Scheduler::current_task();
}

static bool is_user_task() {
    auto* t = task();
    return t && t->page_table_ != 0;
}

static int task_open(vfs::Vnode* vn, uint64_t flags) {
    int fd = task()->fd_table.alloc();
    if (fd < 0) return -1;
    task()->fd_table.fds[fd].vnode = vn;
    task()->fd_table.fds[fd].offset = 0;
    task()->fd_table.fds[fd].flags = flags;
    if (vn->ops->open) vn->ops->open(vn, flags);
    return fd;
}

static int path_open(const char* path, uint64_t flags) {
    vfs::Vnode* vn = vfs::resolve(path);
    if (!vn) return -1;
    return task_open(vn, flags);
}

uint64_t Syscall::handle(uint64_t number, uint64_t arg0, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3, uint64_t* regs)
{
    switch (static_cast<SyscallNumber>(number)) {
    case SyscallNumber::YIELD:
        arch::io_wait();
        Scheduler::reschedule();
        return 0;

    case SyscallNumber::SEND: {
        uint64_t dest_id = arg0;
        Message msg{};
        auto* cur = task();
        msg.sender_id = cur ? cur->id : 0;
        msg.type = arg2;
        msg.data_size = arg3 < IPC_MAX_MSG_SIZE ? arg3 : IPC_MAX_MSG_SIZE;
        auto data = checked(reinterpret_cast<const uint8_t*>(arg1), msg.data_size);
        if (is_user_task() && !data.valid()) return static_cast<uint64_t>(-1);
        for (size_t i = 0; i < msg.data_size; ++i) {
            msg.data[i] = data.read(i);
        }
        return IPC::send(dest_id, msg) ? 0 : static_cast<uint64_t>(-1);
    }

    case SyscallNumber::RECEIVE: {
        uint64_t src_id = arg0;
        uint64_t max_size = arg2;
        auto buf = checked(reinterpret_cast<uint8_t*>(arg1), max_size);
        if (is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1);
        uint8_t* raw_buf = buf.unsafe_ptr();
        Message msg{};
        if (IPC::receive(src_id, msg)) {
            uint64_t copy_size = msg.data_size;
            if (copy_size > max_size) copy_size = max_size;
            for (size_t i = 0; i < copy_size; ++i) raw_buf[i] = msg.data[i];
            return msg.type;
        }
        auto* mb = IPC::find_mailbox(src_id);
        if (!mb) return static_cast<uint64_t>(-1);
        auto* t = task();
        if (!t) return static_cast<uint64_t>(-1);
        mb->waiting_receiver = t;
        t->state = TaskState::BLOCKED;
        Scheduler::reschedule();
        if (IPC::receive(src_id, msg)) {
            uint64_t copy_size = msg.data_size;
            if (copy_size > max_size) copy_size = max_size;
            for (size_t i = 0; i < copy_size; ++i) raw_buf[i] = msg.data[i];
            return msg.type;
        }
        return static_cast<uint64_t>(-1);
    }

    case SyscallNumber::SEND_SYNC: {
        uint64_t dest_id = arg0;
        uint64_t type = arg2;
        uint64_t data_size = arg3 < IPC_MAX_MSG_SIZE ? arg3 : IPC_MAX_MSG_SIZE;
        auto data = checked(reinterpret_cast<const uint8_t*>(arg1), data_size);
        if (is_user_task() && !data.valid()) return static_cast<uint64_t>(-1);
        auto data_rw = checked(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(arg1)), data_size);
        Message msg{};
        auto* cur = task();
        if (!cur) return static_cast<uint64_t>(-1);
        msg.sender_id = cur->id;
        msg.type = type;
        msg.data_size = data_size;
        for (size_t i = 0; i < data_size; ++i) msg.data[i] = data.read(i);
        Message reply{};
        if (!IPC::send_sync(dest_id, msg, reply)) return static_cast<uint64_t>(-1);
        uint64_t copy_size = reply.data_size;
        if (copy_size > IPC_MAX_MSG_SIZE) copy_size = IPC_MAX_MSG_SIZE;
        for (size_t i = 0; i < copy_size; ++i)
            data_rw.write(reply.data[i], i);
        return reply.type;
    }

    case SyscallNumber::PRINT:
        return 0;

    case SyscallNumber::GET_TICKS:
        return arch::Timer::ticks();

    case SyscallNumber::EXIT: {
        auto* t = task();
        if (t) {
            t->state = TaskState::TERMINATED;
            t->exit_code = arg0;
            if (t->parent_id) {
                uint64_t count = Scheduler::task_count();
                for (uint64_t i = 0; i < count; ++i) {
                    auto* p = Scheduler::task_at(i);
                    if (p && p->id == t->parent_id &&
                        (p->waiting_child_pid == t->id ||
                         p->waiting_child_pid == static_cast<uint64_t>(-1))) {
                        if (p->waiting_child_status) {
                            *p->waiting_child_status = t->exit_code;
                            p->waiting_child_status = nullptr;
                        }
                        if (p->context.rsp) {
                            *reinterpret_cast<uint64_t*>(p->context.rsp) = t->id;
                        }
                        p->waiting_child_pid = 0;
                        p->state = TaskState::READY;
                        break;
                    }
                }
            }
        }
        return 0;
    }

    case SyscallNumber::FORK: {
        if (!regs) return static_cast<uint64_t>(-1);
        auto* child = TaskControlBlock::clone(regs);
        if (!child) return static_cast<uint64_t>(-1);
        Scheduler::add_task(child);
        return child->id;
    }

    case SyscallNumber::WAITPID: {
        uint64_t target_pid = arg0;
        auto status = checked(reinterpret_cast<uint64_t*>(arg1));
        auto* status_ptr = (!is_user_task() || status.valid()) ? status.unsafe_ptr() : nullptr;
        auto* cur = task();
        if (!cur) return static_cast<uint64_t>(-1);
        TaskControlBlock* child = nullptr;
        uint64_t count = Scheduler::task_count();
        for (uint64_t i = 0; i < count; ++i) {
            auto* t = Scheduler::task_at(i);
            if (t && t->parent_id == cur->id) {
                if (target_pid == static_cast<uint64_t>(-1) || t->id == target_pid) {
                    child = t;
                    break;
                }
            }
        }
        if (!child) return static_cast<uint64_t>(-1);
        if (child->state == TaskState::TERMINATED) {
            if (status_ptr) *status_ptr = child->exit_code;
            uint64_t cid = child->id;
            child->cleanup();
            Scheduler::remove_task(child);
            delete child;
            return cid;
        }
        if (arg2 & 1) return 0;
        cur->waiting_child_pid = target_pid;
        cur->waiting_child_status = status_ptr;
        cur->state = TaskState::BLOCKED;
        return static_cast<uint64_t>(-1);
    }

    case SyscallNumber::CREATE_MAILBOX: {
        auto* mb = IPC::create_mailbox(arg0);
        return mb ? reinterpret_cast<uint64_t>(mb) : 0;
    }

    case SyscallNumber::DESTROY_MAILBOX:
        IPC::destroy_mailbox(arg0);
        return 0;

    case SyscallNumber::OPEN: {
        const char* user_path = reinterpret_cast<const char*>(arg0);
        int fd;
        if (is_user_task()) {
            char path_buf[MAX_PATH];
            if (!strncpy_from_user(path_buf, user_path, MAX_PATH))
                return static_cast<uint64_t>(-1);
            fd = path_open(path_buf, arg1);
        } else {
            fd = path_open(user_path, arg1);
        }
        return static_cast<uint64_t>(static_cast<int64_t>(fd));
    }

    case SyscallNumber::READ: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f) return static_cast<uint64_t>(-1);
        uint64_t count = arg2;
        auto buf = checked(reinterpret_cast<uint8_t*>(arg1), count);
        if (is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1);
        if (!f->vnode || !f->vnode->ops->read) return static_cast<uint64_t>(-1);
        int64_t r = f->vnode->ops->read(f->vnode, buf.unsafe_ptr(), count, f->offset);
        if (r > 0) f->offset += static_cast<uint64_t>(r);
        return static_cast<uint64_t>(r >= 0 ? r : -1);
    }

    case SyscallNumber::CLOSE: {
        int fd = static_cast<int>(arg0);
        task()->fd_table.free(fd);
        return 0;
    }

    case SyscallNumber::FSTAT: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f || !f->vnode || !f->vnode->ops->fstat) return static_cast<uint64_t>(-1);
        auto* st = reinterpret_cast<vfs::VfsStat*>(arg1);
        return static_cast<uint64_t>(f->vnode->ops->fstat(f->vnode, st));
    }

    case SyscallNumber::WRITE: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f || !f->vnode || !f->vnode->ops->write) return static_cast<uint64_t>(-1);
        uint64_t count = arg2;
        auto buf = checked(reinterpret_cast<const uint8_t*>(arg1), count);
        if (is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1);
        int64_t r = f->vnode->ops->write(f->vnode, buf.unsafe_ptr(), count, f->offset);
        if (r > 0) f->offset += static_cast<uint64_t>(r);
        return static_cast<uint64_t>(r >= 0 ? r : 0);
    }

    case SyscallNumber::LSEEK: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f || !f->vnode || !f->vnode->ops->lseek) return static_cast<uint64_t>(-1);
        int64_t r = f->vnode->ops->lseek(f->vnode,
            static_cast<int64_t>(arg1), static_cast<int>(arg2), &f->offset);
        return static_cast<uint64_t>(r >= 0 ? r : -1);
    }

    case SyscallNumber::IOCTL: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f || !f->vnode || !f->vnode->ops->ioctl) return static_cast<uint64_t>(-1);
        return static_cast<uint64_t>(f->vnode->ops->ioctl(f->vnode, arg1, reinterpret_cast<void*>(arg2)));
    }

    case SyscallNumber::READDIR: {
        auto* f = task()->fd_table.get(static_cast<int>(arg0));
        if (!f || !f->vnode || !f->vnode->ops->readdir) return static_cast<uint64_t>(-1);
        auto pos_chk = checked(reinterpret_cast<uint64_t*>(arg1));
        auto dent_chk = checked(reinterpret_cast<vfs::Dirent*>(arg2));
        if (is_user_task() && (!pos_chk.valid() || !dent_chk.valid())) return static_cast<uint64_t>(-1);
        uint64_t p = pos_chk.read();
        int r = f->vnode->ops->readdir(f->vnode, &p, dent_chk.unsafe_ptr());
        if (r == 0) pos_chk.write(p);
        return static_cast<uint64_t>(r == 0 ? 0 : -1);
    }

    case SyscallNumber::STAT: {
        vfs::Vnode* vn;
        if (is_user_task()) {
            char path_buf[MAX_PATH];
            if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0), MAX_PATH))
                return static_cast<uint64_t>(-1);
            vn = vfs::resolve(path_buf);
        } else {
            vn = vfs::resolve(reinterpret_cast<const char*>(arg0));
        }
        if (!vn || !vn->ops->fstat) return static_cast<uint64_t>(-1);
        auto st = checked(reinterpret_cast<vfs::VfsStat*>(arg1));
        if (is_user_task() && !st.valid()) return static_cast<uint64_t>(-1);
        return static_cast<uint64_t>(vn->ops->fstat(vn, st.unsafe_ptr()));
    }

    case SyscallNumber::DUP: {
        int old_fd = static_cast<int>(arg0);
        auto* old = task()->fd_table.get(old_fd);
        if (!old) return static_cast<uint64_t>(-1);
        int new_fd = task()->fd_table.alloc();
        if (new_fd < 0) return static_cast<uint64_t>(-1);
        task()->fd_table.fds[new_fd] = task()->fd_table.fds[old_fd];
        if (old->vnode && old->vnode->refcount > 0)
            ++old->vnode->refcount;
        return static_cast<uint64_t>(new_fd);
    }

    case SyscallNumber::CHDIR: {
        vfs::Vnode* vn;
        const char* resolved_path;
        char path_buf[MAX_PATH];
        if (is_user_task()) {
            if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0), MAX_PATH))
                return static_cast<uint64_t>(-1);
            vn = vfs::resolve(path_buf);
            resolved_path = path_buf;
        } else {
            auto* raw = reinterpret_cast<const char*>(arg0);
            vn = vfs::resolve(raw);
            resolved_path = raw;
        }
        if (!vn) return static_cast<uint64_t>(-1);
        if (!(vn->mode & vfs::S_IFDIR)) return static_cast<uint64_t>(-1);
        task()->cwd_vnode = vn;
        size_t i = 0;
        while (resolved_path[i] && i < 255) { task()->cwd[i] = resolved_path[i]; ++i; }
        task()->cwd[i] = '\0';
        return 0;
    }

    case SyscallNumber::EXEC: {
        vfs::Vnode* vn;
        if (is_user_task()) {
            char path_buf[MAX_PATH];
            if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0), MAX_PATH))
                return static_cast<uint64_t>(-1);
            vn = vfs::resolve(path_buf);
        } else {
            vn = vfs::resolve(reinterpret_cast<const char*>(arg0));
        }
        auto* argv = reinterpret_cast<const char* const*>(arg1);
        auto* envp = reinterpret_cast<const char* const*>(arg2);
        if (!vn) return static_cast<uint64_t>(-1);
        if (vn->size == 0 || vn->size > 512_KiB) return static_cast<uint64_t>(-1);
        size_t file_pages = (static_cast<size_t>(vn->size) + 4095) / 4096;
        uint64_t file_phys = PMM::alloc_contiguous(file_pages);
        if (!file_phys) return static_cast<uint64_t>(-1);
        uint8_t* file_buf = reinterpret_cast<uint8_t*>(file_phys);
        int64_t r = vn->ops->read(vn, file_buf, vn->size, 0);
        if (r <= 0 || static_cast<uint64_t>(r) != vn->size) {
            for (size_t i = 0; i < file_pages; ++i)
                PMM::free_page(file_phys + i * 4096);
            return static_cast<uint64_t>(-1);
        }
        auto* hdr = reinterpret_cast<const elf::ELF64Header*>(file_buf);
        if (!elf::exec_into_current(hdr, file_buf, argv, envp, regs)) {
            for (size_t i = 0; i < file_pages; ++i)
                PMM::free_page(file_phys + i * 4096);
            return static_cast<uint64_t>(-1);
        }
        for (size_t i = 0; i < file_pages; ++i)
            PMM::free_page(file_phys + i * 4096);
        return 0;
    }

    case SyscallNumber::GETPID: {
        auto* t = task();
        return t ? t->id : 0;
    }

    case SyscallNumber::KILL: {
        uint64_t target_pid = arg0;
        uint64_t sig = arg1;
        uint64_t count = Scheduler::task_count();
        for (uint64_t i = 0; i < count; ++i) {
            auto* t = Scheduler::task_at(i);
            if (t && t->id == target_pid) {
                t->state = TaskState::TERMINATED;
                t->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
                if (t->parent_id) {
                    for (uint64_t j = 0; j < count; ++j) {
                        auto* p = Scheduler::task_at(j);
                        if (p && p->id == t->parent_id &&
                            (p->waiting_child_pid == t->id ||
                             p->waiting_child_pid == static_cast<uint64_t>(-1))) {
                            if (p->context.rsp) {
                                *reinterpret_cast<uint64_t*>(p->context.rsp) = t->id;
                            }
                            p->waiting_child_pid = 0;
                            p->state = TaskState::READY;
                            break;
                        }
                    }
                }
                return 0;
            }
        }
        return static_cast<uint64_t>(-1);
    }

    case SyscallNumber::PIPE: {
        auto fds_buf = checked(reinterpret_cast<int*>(arg0), 2 * sizeof(int));
        if (is_user_task() && !fds_buf.valid()) return static_cast<uint64_t>(-1);
        int fds[2];
        int result = vfs::create_pipe(fds);
        if (result < 0) return static_cast<uint64_t>(-1);
        fds_buf.write(static_cast<uint64_t>(fds[0]), 0);
        fds_buf.write(static_cast<uint64_t>(fds[1]), sizeof(int));
        return 0;
    }

    case SyscallNumber::DUP2: {
        int old_fd = static_cast<int>(arg0);
        int new_fd = static_cast<int>(arg1);
        auto* t = task();
        if (!t) return static_cast<uint64_t>(-1);
        auto* old_desc = t->fd_table.get(old_fd);
        if (!old_desc) return static_cast<uint64_t>(-1);
        if (old_fd == new_fd) return static_cast<uint64_t>(new_fd);
        if (new_fd < 0 || static_cast<size_t>(new_fd) >= vfs::MAX_FDS) return static_cast<uint64_t>(-1);
        t->fd_table.free(new_fd);
        t->fd_table.fds[new_fd] = *old_desc;
        t->fd_table.fds[new_fd].used = true;
        if (old_desc->vnode && old_desc->vnode->refcount > 0)
            ++old_desc->vnode->refcount;
        return static_cast<uint64_t>(new_fd);
    }

    default:
        return static_cast<uint64_t>(-1);
    }
}

} // namespace kernel
