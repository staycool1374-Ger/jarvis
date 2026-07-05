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

#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/vfs/pipe.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <string.hpp>

namespace kernel {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool vfsd_authorize(uint64_t op_type, uint64_t pid, const char* path) {
    uint64_t vfsd_pid = vfsd::get_vfsd_pid();
    if (vfsd_pid == 0) return false;
    if (vfsd::is_vfsd_task()) return true;

    // Kernel tasks (no page table) are trusted — bypass IPC authorization
    auto* cur = kernel::Scheduler::current_task();
    if (cur && !cur->page_table_) return true;

    vfsd::Msg msg{};
    msg.sender_id = pid;
    msg.type = op_type;
    msg.arg0 = 0;
    size_t i = 0;
    if (path) {
        while (path[i] && i < sizeof(msg.path) - 1) {
            msg.path[i] = path[i];
            ++i;
        }
    }
    msg.path[i] = '\0';

    Message send_msg{};
    send_msg.sender_id = pid;
    send_msg.type = op_type;
    send_msg.data_size = sizeof(vfsd::Msg);
    __builtin_memcpy(send_msg.data, &msg, sizeof(vfsd::Msg));

    Message reply_msg{};
    if (!IPC::send_sync(vfsd_pid, send_msg, reply_msg)) return false;
    if (reply_msg.data_size < sizeof(vfsd::Reply)) return false;

    vfsd::Reply reply{};
    __builtin_memcpy(&reply, reply_msg.data, sizeof(vfsd::Reply));
    return reply.result >= 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool vfsd_authorize_fd_op(uint64_t op_type, uint64_t pid, int fd) {
    char fd_str[16] = {0};
    int len = 0;
    int n = fd;
    if (n == 0) { fd_str[len++] = '0'; }
    else {
        char tmp[16];
        int tmp_len = 0;
        while (n > 0) { tmp[tmp_len++] = static_cast<char>('0' + (n % 10)); n /= 10; }
        for (int j = tmp_len - 1; j >= 0; --j) fd_str[len++] = tmp[j];
    }
    fd_str[len] = '\0';

    uint64_t vfsd_pid = vfsd::get_vfsd_pid();
    if (vfsd_pid == 0) return false;
    if (vfsd::is_vfsd_task()) return true;

    // Kernel tasks (no page table) are trusted — bypass IPC authorization
    auto* cur = kernel::Scheduler::current_task();
    if (cur && !cur->page_table_) return true;

    vfsd::Msg msg{};
    msg.sender_id = pid;
    msg.type = op_type;
    msg.arg0 = static_cast<uint64_t>(fd);
    size_t i = 0;
    while (fd_str[i] && i < sizeof(msg.path) - 1) {
        msg.path[i] = fd_str[i];
        ++i;
    }
    msg.path[i] = '\0';

    Message send_msg{};
    send_msg.sender_id = pid;
    send_msg.type = op_type;
    send_msg.data_size = sizeof(vfsd::Msg);
    __builtin_memcpy(send_msg.data, &msg, sizeof(vfsd::Msg));

    Message reply_msg{};
    if (!IPC::send_sync(vfsd_pid, send_msg, reply_msg)) return false;
    if (reply_msg.data_size < sizeof(vfsd::Reply)) return false;

    vfsd::Reply reply{};
    __builtin_memcpy(&reply, reply_msg.data, sizeof(vfsd::Reply));
    return reply.result >= 0;
}

uint64_t Syscall::sys_open(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    int fd = -1;
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_OPEN, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        fd = syscall_path_open(path_buf, arg1);
    } else {
        if (!vfsd_authorize(vfsd::VFS_OPEN, syscall_task() ? syscall_task(
            )->id : 0, user_path))
            return static_cast<uint64_t>(-1);
        fd = syscall_path_open(user_path, arg1);
    }
    return static_cast<uint64_t>(static_cast<int64_t>(fd));
}

uint64_t Syscall::sys_read(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    if (!vfsd_authorize_fd_op(vfsd::VFS_READ, cur->id, static_cast<int>(arg0)))
        return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f) return static_cast<uint64_t>(-1);
    uint64_t count = arg2;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto buf = checked(reinterpret_cast<uint8_t*>(arg1), count);
    if (syscall_is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1
        );
    if (!f->vnode || !f->vnode->ops->read) return static_cast<uint64_t>(-1);
    int64_t r = f->vnode->ops->read(*f->vnode, buf.unsafe_ptr(), count,
        f->offset);
    if (r > 0) f->offset += static_cast<uint64_t>(r);
    return static_cast<uint64_t>(r >= 0 ? r : -1);
}

uint64_t Syscall::sys_close(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    if (!vfsd_authorize_fd_op(vfsd::VFS_CLOSE, cur->id, static_cast<int>(arg0)))
        return static_cast<uint64_t>(-1);
    int fd = static_cast<int>(arg0);
    cur->fd_table.free(fd);
    return 0;
}

uint64_t Syscall::sys_fstat(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    if (!vfsd_authorize_fd_op(vfsd::VFS_FSTAT, cur->id, static_cast<int>(arg0)))
        return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->fstat) return static_cast<uint64_t>(
        -1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* st = reinterpret_cast<vfs::VfsStat*>(arg1);
    return static_cast<uint64_t>(f->vnode->ops->fstat(*f->vnode, *st));
}

uint64_t Syscall::sys_write(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    if (!vfsd_authorize_fd_op(vfsd::VFS_WRITE, cur->id, static_cast<int>(arg0)))
        return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->write) return static_cast<uint64_t>(
        -1);
    uint64_t count = arg2;
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto buf = checked(reinterpret_cast<const uint8_t*>(arg1), count);
    if (syscall_is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1
        );
    int64_t r = f->vnode->ops->write(*f->vnode, buf.unsafe_ptr(), count,
        f->offset);
    if (r > 0) f->offset += static_cast<uint64_t>(r);
    return static_cast<uint64_t>(r >= 0 ? r : 0);
}

uint64_t Syscall::sys_lseek(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->lseek) return static_cast<uint64_t>(
        -1);
    int64_t r = f->vnode->ops->lseek(*f->vnode,
        static_cast<int64_t>(arg1), static_cast<int>(arg2), &f->offset);
    return static_cast<uint64_t>(r >= 0 ? r : -1);
}

uint64_t Syscall::sys_ioctl(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->ioctl) return static_cast<uint64_t>(
        -1);
    return static_cast<uint64_t>(f->vnode->ops->ioctl(*f->vnode, arg1,
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        reinterpret_cast<void*>(arg2)));
}

uint64_t Syscall::sys_readdir(uint64_t arg0, uint64_t arg1, uint64_t arg2,
    uint64_t, uint64_t*) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    auto* f = cur->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->readdir
        ) return static_cast<uint64_t>(-1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto pos_chk = checked(reinterpret_cast<uint64_t*>(arg1));
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto dent_chk = checked(reinterpret_cast<vfs::Dirent*>(arg2));
    if (syscall_is_user_task() && (!pos_chk.valid() || !dent_chk.valid())
        ) return static_cast<uint64_t>(-1);
    uint64_t position = pos_chk.read();
    int r = f->vnode->ops->readdir(*f->vnode, position, *dent_chk.unsafe_ptr());
    if (r == 0) pos_chk.write(position);
    return static_cast<uint64_t>(r == 0 ? 0 : -1);
}

uint64_t Syscall::sys_stat(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    vfs::Vnode* vn = nullptr;
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_STAT, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        vn = vfs::resolve(path_buf);
    } else {
        if (!vfsd_authorize(vfsd::VFS_STAT, syscall_task() ? syscall_task(
            )->id : 0, user_path))
            return static_cast<uint64_t>(-1);
        vn = vfs::resolve(user_path);
    }
    if (!vn || !vn->ops->fstat) return static_cast<uint64_t>(-1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto st = checked(reinterpret_cast<vfs::VfsStat*>(arg1));
    if (syscall_is_user_task() && !st.valid()) return static_cast<uint64_t>(-1);
    return static_cast<uint64_t>(vn->ops->fstat(*vn, *st.unsafe_ptr()));
}

uint64_t Syscall::sys_dup(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*
    ) {
    auto* cur = syscall_task();
    if (!cur) return static_cast<uint64_t>(-1);
    int old_fd = static_cast<int>(arg0);
    auto* old = cur->fd_table.get(old_fd);
    if (!old) return static_cast<uint64_t>(-1);
    int new_fd = cur->fd_table.alloc();
    if (new_fd < 0) return static_cast<uint64_t>(-1);
    cur->fd_table.fds[new_fd] = cur->fd_table.fds[old_fd];
    if (old->vnode && old->vnode->refcount > 0)
        ++old->vnode->refcount;
    return static_cast<uint64_t>(new_fd);
}

uint64_t Syscall::sys_chdir(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    vfs::Vnode* vn = nullptr;
    const char* resolved_path = nullptr;
    char path_buf[SYSCALL_MAX_PATH];
    if (syscall_is_user_task()) {
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_CHDIR, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        vn = vfs::resolve(path_buf);
        resolved_path = path_buf;
    } else {
        if (!vfsd_authorize(vfsd::VFS_CHDIR, syscall_task() ? syscall_task(
            )->id : 0, user_path))
            return static_cast<uint64_t>(-1);
        vn = vfs::resolve(user_path);
        resolved_path = user_path;
    }
    auto* cur = syscall_task();
    if (!cur || !vn) return static_cast<uint64_t>(-1);
    if (!(vn->mode & vfs::S_IFDIR)) return static_cast<uint64_t>(-1);
    cur->cwd_vnode = vn;
    size_t i = 0;
    while (resolved_path[i] && i < 255) { cur->cwd[i] = resolved_path[i]; ++i; }
    cur->cwd[i] = '\0';
    return 0;
}

uint64_t Syscall::sys_pipe(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    int fds[2];
    int result = vfs::create_pipe(fds);
    if (result < 0) return static_cast<uint64_t>(-1);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* out = reinterpret_cast<int*>(arg0);
    if (syscall_is_user_task()) {
        auto fds_buf = checked(out, 2);
        if (!fds_buf.valid()) return static_cast<uint64_t>(-1);
        if (!fds_buf.write(fds[0], 0)) return static_cast<uint64_t>(-1);
        if (!fds_buf.write(fds[1], 1)) return static_cast<uint64_t>(-1);
    } else {
        out[0] = fds[0];
        out[1] = fds[1];
    }
    return 0;
}

uint64_t Syscall::sys_dup2(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    int old_fd = static_cast<int>(arg0);
    int new_fd = static_cast<int>(arg1);
    auto* t = syscall_task();
    if (!t) return static_cast<uint64_t>(-1);
    auto* old_desc = t->fd_table.get(old_fd);
    if (!old_desc) return static_cast<uint64_t>(-1);
    if (old_fd == new_fd) return static_cast<uint64_t>(new_fd);
    if (new_fd < 0 || static_cast<size_t>(new_fd) >= vfs::MAX_FDS
        ) return static_cast<uint64_t>(-1);
    t->fd_table.free(new_fd);
    t->fd_table.fds[new_fd] = *old_desc;
    t->fd_table.fds[new_fd].used = true;
    if (old_desc->vnode && old_desc->vnode->refcount > 0)
        ++old_desc->vnode->refcount;
    return static_cast<uint64_t>(new_fd);
}

uint64_t Syscall::sys_mkdir(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t,
    uint64_t*) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_MKDIR, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        int r = vfs::mkdir(path_buf, static_cast<uint16_t>(arg1));
        return static_cast<uint64_t>(r == 0 ? 0 : -1);
    }
    if (!vfsd_authorize(vfsd::VFS_MKDIR, syscall_task() ? syscall_task(
        )->id : 0, user_path))
        return static_cast<uint64_t>(-1);
    int r = vfs::mkdir(user_path, static_cast<uint16_t>(arg1));
    return static_cast<uint64_t>(r == 0 ? 0 : -1);
}

uint64_t Syscall::sys_unlink(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_UNLINK, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        int r = vfs::unlink(path_buf);
        return static_cast<uint64_t>(r == 0 ? 0 : -1);
    }
    if (!vfsd_authorize(vfsd::VFS_UNLINK, syscall_task() ? syscall_task(
        )->id : 0, user_path))
        return static_cast<uint64_t>(-1);
    int r = vfs::unlink(user_path);
    return static_cast<uint64_t>(r == 0 ? 0 : -1);
}

uint64_t Syscall::sys_rmdir(uint64_t arg0, uint64_t, uint64_t, uint64_t,
    uint64_t*) {
    // rmdir is just unlink with directory semantics (enforced by FS)
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const char* user_path = reinterpret_cast<const char*>(arg0);
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        if (!vfsd_authorize(vfsd::VFS_RMDIR, syscall_task() ? syscall_task(
            )->id : 0, path_buf))
            return static_cast<uint64_t>(-1);
        int r = vfs::unlink(path_buf);
        return static_cast<uint64_t>(r == 0 ? 0 : -1);
    }
    if (!vfsd_authorize(vfsd::VFS_RMDIR, syscall_task() ? syscall_task(
        )->id : 0, user_path))
        return static_cast<uint64_t>(-1);
    int r = vfs::unlink(user_path);
    return static_cast<uint64_t>(r == 0 ? 0 : -1);
}

} // namespace kernel
