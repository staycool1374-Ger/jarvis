#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/task/task.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/pipe.hpp>
#include <kernel/memory/checked_ptr.hpp>
#include <string.hpp>

namespace kernel {

uint64_t Syscall::sys_open(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    const char* user_path = reinterpret_cast<const char*>(arg0);
    int fd;
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, user_path, SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        fd = syscall_path_open(path_buf, arg1);
    } else {
        fd = syscall_path_open(user_path, arg1);
    }
    return static_cast<uint64_t>(static_cast<int64_t>(fd));
}

uint64_t Syscall::sys_read(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f) return static_cast<uint64_t>(-1);
    uint64_t count = arg2;
    auto buf = checked(reinterpret_cast<uint8_t*>(arg1), count);
    if (syscall_is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1);
    if (!f->vnode || !f->vnode->ops->read) return static_cast<uint64_t>(-1);
    int64_t r = f->vnode->ops->read(f->vnode, buf.unsafe_ptr(), count, f->offset);
    if (r > 0) f->offset += static_cast<uint64_t>(r);
    return static_cast<uint64_t>(r >= 0 ? r : -1);
}

uint64_t Syscall::sys_close(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    int fd = static_cast<int>(arg0);
    syscall_task()->fd_table.free(fd);
    return 0;
}

uint64_t Syscall::sys_fstat(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->fstat) return static_cast<uint64_t>(-1);
    auto* st = reinterpret_cast<vfs::VfsStat*>(arg1);
    return static_cast<uint64_t>(f->vnode->ops->fstat(f->vnode, st));
}

uint64_t Syscall::sys_write(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->write) return static_cast<uint64_t>(-1);
    uint64_t count = arg2;
    auto buf = checked(reinterpret_cast<const uint8_t*>(arg1), count);
    if (syscall_is_user_task() && !buf.valid()) return static_cast<uint64_t>(-1);
    int64_t r = f->vnode->ops->write(f->vnode, buf.unsafe_ptr(), count, f->offset);
    if (r > 0) f->offset += static_cast<uint64_t>(r);
    return static_cast<uint64_t>(r >= 0 ? r : 0);
}

uint64_t Syscall::sys_lseek(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->lseek) return static_cast<uint64_t>(-1);
    int64_t r = f->vnode->ops->lseek(f->vnode,
        static_cast<int64_t>(arg1), static_cast<int>(arg2), &f->offset);
    return static_cast<uint64_t>(r >= 0 ? r : -1);
}

uint64_t Syscall::sys_ioctl(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->ioctl) return static_cast<uint64_t>(-1);
    return static_cast<uint64_t>(f->vnode->ops->ioctl(f->vnode, arg1, reinterpret_cast<void*>(arg2)));
}

uint64_t Syscall::sys_readdir(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t, uint64_t*) {
    auto* f = syscall_task()->fd_table.get(static_cast<int>(arg0));
    if (!f || !f->vnode || !f->vnode->ops->readdir) return static_cast<uint64_t>(-1);
    auto pos_chk = checked(reinterpret_cast<uint64_t*>(arg1));
    auto dent_chk = checked(reinterpret_cast<vfs::Dirent*>(arg2));
    if (syscall_is_user_task() && (!pos_chk.valid() || !dent_chk.valid())) return static_cast<uint64_t>(-1);
    uint64_t p = pos_chk.read();
    int r = f->vnode->ops->readdir(f->vnode, &p, dent_chk.unsafe_ptr());
    if (r == 0) pos_chk.write(p);
    return static_cast<uint64_t>(r == 0 ? 0 : -1);
}

uint64_t Syscall::sys_stat(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    vfs::Vnode* vn;
    if (syscall_is_user_task()) {
        char path_buf[SYSCALL_MAX_PATH];
        if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0), SYSCALL_MAX_PATH))
            return static_cast<uint64_t>(-1);
        vn = vfs::resolve(path_buf);
    } else {
        vn = vfs::resolve(reinterpret_cast<const char*>(arg0));
    }
    if (!vn || !vn->ops->fstat) return static_cast<uint64_t>(-1);
    auto st = checked(reinterpret_cast<vfs::VfsStat*>(arg1));
    if (syscall_is_user_task() && !st.valid()) return static_cast<uint64_t>(-1);
    return static_cast<uint64_t>(vn->ops->fstat(vn, st.unsafe_ptr()));
}

uint64_t Syscall::sys_dup(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    int old_fd = static_cast<int>(arg0);
    auto* old = syscall_task()->fd_table.get(old_fd);
    if (!old) return static_cast<uint64_t>(-1);
    int new_fd = syscall_task()->fd_table.alloc();
    if (new_fd < 0) return static_cast<uint64_t>(-1);
    syscall_task()->fd_table.fds[new_fd] = syscall_task()->fd_table.fds[old_fd];
    if (old->vnode && old->vnode->refcount > 0)
        ++old->vnode->refcount;
    return static_cast<uint64_t>(new_fd);
}

uint64_t Syscall::sys_chdir(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    vfs::Vnode* vn;
    const char* resolved_path;
    char path_buf[SYSCALL_MAX_PATH];
    if (syscall_is_user_task()) {
        if (!strncpy_from_user(path_buf, reinterpret_cast<const char*>(arg0), SYSCALL_MAX_PATH))
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
    syscall_task()->cwd_vnode = vn;
    size_t i = 0;
    while (resolved_path[i] && i < 255) { syscall_task()->cwd[i] = resolved_path[i]; ++i; }
    syscall_task()->cwd[i] = '\0';
    return 0;
}

uint64_t Syscall::sys_pipe(uint64_t arg0, uint64_t, uint64_t, uint64_t, uint64_t*) {
    auto fds_buf = checked(reinterpret_cast<int*>(arg0), 2 * sizeof(int));
    if (syscall_is_user_task() && !fds_buf.valid()) return static_cast<uint64_t>(-1);
    int fds[2];
    int result = vfs::create_pipe(fds);
    if (result < 0) return static_cast<uint64_t>(-1);
    fds_buf.write(static_cast<uint64_t>(fds[0]), 0);
    fds_buf.write(static_cast<uint64_t>(fds[1]), sizeof(int));
    return 0;
}

uint64_t Syscall::sys_dup2(uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, uint64_t*) {
    int old_fd = static_cast<int>(arg0);
    int new_fd = static_cast<int>(arg1);
    auto* t = syscall_task();
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

} // namespace kernel
