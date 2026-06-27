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

#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <kernel/vfs/vfs_errors.hpp>

using namespace kernel::errors;

namespace kernel {
namespace vfs {

static Mount mount_table[MAX_MOUNTS];
static size_t mount_count = 0;
static Vnode* root_vnode_global = nullptr;

Vnode* get_root_vnode() {
    return root_vnode_global;
}

void set_root_vnode(Vnode& vnode) {
    root_vnode_global = &vnode;
}

int FdTable::alloc() {
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (!fds[i].used) {
            fds[i].used = true;
            fds[i].vnode = nullptr;
            fds[i].offset = 0;
            fds[i].flags = 0;
            kernel::test::ResourceTracker::instance().track_fd_add();
            return static_cast<int>(i);
        }
    }
    return VFS_INVALID;
}

VfsError FdTable::alloc_err(int& out_fd) {
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (!fds[i].used) {
            fds[i].used = true;
            fds[i].vnode = nullptr;
            fds[i].offset = 0;
            fds[i].flags = 0;
            kernel::test::ResourceTracker::instance().track_fd_add();
            out_fd = static_cast<int>(i);
            return VFS_ERR_OK;
        }
    }
    return VFS_ERR_FD_TABLE_FULL;
}

void FdTable::free(int file_descriptor) {
    if (file_descriptor < 0 || static_cast<size_t>(file_descriptor) >= MAX_FDS
        ) return;
    if (fds[file_descriptor].used && fds[file_descriptor].vnode) {
        if (fds[file_descriptor].vnode->refcount > 0) {
            --fds[file_descriptor].vnode->refcount;
            if (fds[file_descriptor].vnode->refcount == 0) {
                if (fds[file_descriptor].vnode->ops->close)
                    fds[file_descriptor].vnode->ops->close(*fds[file_descriptor
                        ].vnode);
            }
        } else {
            if (fds[file_descriptor].vnode->ops->close)
                fds[file_descriptor].vnode->ops->close(*fds[file_descriptor
                    ].vnode);
        }
    }
    if (fds[file_descriptor].used) {
        kernel::test::ResourceTracker::instance().track_fd_remove();
    }
    fds[file_descriptor].used = false;
    fds[file_descriptor].vnode = nullptr;
    fds[file_descriptor].offset = 0;
    fds[file_descriptor].flags = 0;
}

VfsError FdTable::free_err(int file_descriptor) {
    if (file_descriptor < 0 || static_cast<size_t>(file_descriptor) >= MAX_FDS
        ) return VFS_ERR_INVALID_FD;
    if (fds[file_descriptor].used && fds[file_descriptor].vnode) {
        if (fds[file_descriptor].vnode->refcount > 0) {
            --fds[file_descriptor].vnode->refcount;
            if (fds[file_descriptor].vnode->refcount == 0) {
                if (fds[file_descriptor].vnode->ops->close)
                    fds[file_descriptor].vnode->ops->close(*fds[file_descriptor
                        ].vnode);
            }
        } else {
            if (fds[file_descriptor].vnode->ops->close)
                fds[file_descriptor].vnode->ops->close(*fds[file_descriptor
                    ].vnode);
        }
    }
    if (fds[file_descriptor].used) {
        kernel::test::ResourceTracker::instance().track_fd_remove();
    }
    fds[file_descriptor].used = false;
    fds[file_descriptor].vnode = nullptr;
    fds[file_descriptor].offset = 0;
    fds[file_descriptor].flags = 0;
    return VFS_ERR_OK;
}

FileDescription* FdTable::get(int file_descriptor) {
    if (file_descriptor < 0 || static_cast<size_t>(file_descriptor) >= MAX_FDS
        ) return nullptr;
    if (!fds[file_descriptor].used) return nullptr;
    return &fds[file_descriptor];
}

VfsError FdTable::get_err(int file_descriptor, FileDescription*& out_fd) {
    if (file_descriptor < 0 || static_cast<size_t>(file_descriptor) >= MAX_FDS
        ) return VFS_ERR_INVALID_FD;
    if (!fds[file_descriptor].used) return VFS_ERR_INVALID_FD;
    out_fd = &fds[file_descriptor];
    return VFS_ERR_OK;
}

Vnode* resolve(const char* path) {
    if (!path || !*path) return nullptr;

    TaskControlBlock* task = Scheduler::current_task();

    bool is_absolute = (path[0] == '/');
    const char* search = path;
    Vnode* current = nullptr;

    if (is_absolute) {
        Mount* best = nullptr;
        size_t best_len = 0;
        for (size_t i = 0; i < mount_count; ++i) {
            if (!mount_table[i].used) continue;
            const char* mp = mount_table[i].mount_point;
            size_t mplen = strlen(mp);
            if (strncmp(path, mp, mplen) == 0) {
                if (path[mplen] == '/' || path[mplen] == '\0') {
                    if (mplen > best_len) {
                        best = &mount_table[i];
                        best_len = mplen;
                    }
                }
            }
        }
        if (!best) return nullptr;
        current = best->root_vnode;
        search = path + best_len;
        while (*search == '/') ++search;
    } else {
        current = (task && task->cwd_vnode) ? task->cwd_vnode :
            root_vnode_global;
    }

    if (!current) return nullptr;

    char comp[MAX_PATH];
    while (*search) {
        while (*search == '/') ++search;
        if (!*search) break;

        size_t i = 0;
        while (*search && *search != '/' && i < MAX_PATH - 1) {
            comp[i++] = *search++;
        }
        comp[i] = '\0';

        if (comp[0] == '.' && comp[1] == '\0') continue;

        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            if (current->private_data) {
                Vnode* pv = reinterpret_cast<Vnode*>(current->private_data);
                if (pv) { current = pv; continue; }
            }
            for (size_t i = 0; i < mount_count; ++i) {
                if (!mount_table[i].used) continue;
                if (mount_table[i].root_vnode != current) continue;
                const char* mp = mount_table[i].mount_point;
                const char* last_slash = nullptr;
                for (const char* p = mp; *p; ++p) {
                    if (*p == '/') last_slash = p;
                }
                if (last_slash && last_slash > mp) {
                    size_t len = static_cast<size_t>(last_slash - mp);
                    if (len >= MAX_PATH) len = MAX_PATH - 1;
                    char parent_path[MAX_PATH];
                    __builtin_memcpy(parent_path, mp, len);
                    parent_path[len] = '\0';
                    current = resolve(parent_path);
                } else if (last_slash == mp) {
                    current = resolve("/");
                }
                break;
            }
            continue;
        }

        Vnode* child = nullptr;
        for (size_t i = 0; i < mount_count; ++i) {
            if (!mount_table[i].used) continue;
            const char* mp = mount_table[i].mount_point;
            const char* name_start = mp;
            for (const char* p = mp; *p; ++p) {
                if (*p == '/') name_start = p + 1;
            }
            if (strcmp(name_start, comp) == 0) {
                child = mount_table[i].root_vnode;
                break;
            }
        }
        if (!child) {
            child = current->ops ? current->ops->lookup(*current, comp) :
                nullptr;
        }
        if (!child) return nullptr;
        current = child;
    }

    return current;
}

int mount(Filesystem& filesystem, const char* mount_point) {
    if (!filesystem.get_root || mount_count >= MAX_MOUNTS) return VFS_INVALID;

    Vnode* root = filesystem.get_root();
    if (!root) return VFS_INVALID;

    mount_table[mount_count].mount_point = mount_point;
    mount_table[mount_count].fs = &filesystem;
    mount_table[mount_count].root_vnode = root;
    mount_table[mount_count].used = true;
    ++mount_count;

    if (mount_count == 1) {
        root_vnode_global = root;
    }

    return 0;
}

void init() {
    mount_count = 0;
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        mount_table[i].used = false;
        mount_table[i].mount_point = nullptr;
        mount_table[i].fs = nullptr;
        mount_table[i].root_vnode = nullptr;
    }
}

Filesystem* find_fs(const char* name) {
    for (size_t i = 0; i < mount_count; ++i) {
        if (mount_table[i].fs &&
            mount_table[i].fs->name &&
            strcmp(mount_table[i].fs->name, name) == 0) {
            return mount_table[i].fs;
        }
    }
    return nullptr;
}

int mkdir(const char* path, uint16_t mode) {
    // Resolve the parent directory
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }

    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];

    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }

    if (!*name) return VFS_INVALID;

    Vnode* parent = resolve(parent_path);
    if (!parent || !(parent->mode & S_IFDIR)) return VFS_INVALID;
    if (!parent->ops || !parent->ops->mkdir) return VFS_INVALID;

    return parent->ops->mkdir(*parent, name, mode);
}

int unlink(const char* path) {
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }

    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];

    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }

    if (!*name) return VFS_INVALID;

    Vnode* parent = resolve(parent_path);
    if (!parent || !(parent->mode & S_IFDIR)) return VFS_INVALID;
    if (!parent->ops || !parent->ops->unlink) return VFS_INVALID;

    return parent->ops->unlink(*parent, name);
}

int create(const char* path, uint16_t mode) {
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }
    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];
    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }
    if (!*name) return VFS_INVALID;
    Vnode* parent = resolve(parent_path);
    if (!parent || !(parent->mode & S_IFDIR)) return VFS_INVALID;
    if (!parent->ops || !parent->ops->create) return VFS_INVALID;
    return parent->ops->create(*parent, name, mode);
}

VfsError mount_err(Filesystem& filesystem, const char* mount_point) {
    if (!filesystem.get_root || mount_count >= MAX_MOUNTS) {
        return VFS_ERR_INVALID_ARGS;
    }

    Vnode* root = filesystem.get_root();
    if (!root) {
        return VFS_ERR_NO_DEVICE;
    }

    mount_table[mount_count].mount_point = mount_point;
    mount_table[mount_count].fs = &filesystem;
    mount_table[mount_count].root_vnode = root;
    mount_table[mount_count].used = true;
    ++mount_count;

    if (mount_count == 1) {
        root_vnode_global = root;
    }

    return VFS_ERR_OK;
}

VfsError init_err() {
    mount_count = 0;
    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        mount_table[i].used = false;
        mount_table[i].mount_point = nullptr;
        mount_table[i].fs = nullptr;
        mount_table[i].root_vnode = nullptr;
    }
    root_vnode_global = nullptr;
    return VFS_ERR_OK;
}

VfsError find_fs_err(const char* name, Filesystem*& out_fs) {
    if (!name) {
        return VFS_ERR_INVALID_ARGS;
    }
    for (size_t i = 0; i < mount_count; ++i) {
        if (mount_table[i].fs &&
            mount_table[i].fs->name &&
            strcmp(mount_table[i].fs->name, name) == 0) {
            out_fs = mount_table[i].fs;
            return VFS_ERR_OK;
        }
    }
    return VFS_ERR_NO_SUCH_FS;
}

VfsError set_root_vnode_err(Vnode& vnode) {
    root_vnode_global = &vnode;
    return VFS_ERR_OK;
}

VfsError resolve_err(const char* path, Vnode*& out_vnode) {
    Vnode* result = resolve(path);
    if (!result) {
        return VFS_ERR_NOT_FOUND;
    }
    out_vnode = result;
    return VFS_ERR_OK;
}

VfsError mkdir_err(const char* path, uint16_t mode) {
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }

    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];

    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }

    if (!*name) return VFS_ERR_INVALID_ARGS;

    Vnode* parent = resolve(parent_path);
    if (!parent) return VFS_ERR_NOT_FOUND;
    if (!(parent->mode & S_IFDIR)) return VFS_ERR_NOT_DIR;
    if (!parent->ops || !parent->ops->mkdir) return VFS_ERR_NOT_SUPPORTED;

    int result = parent->ops->mkdir(*parent, name, mode);
    return result == 0 ? VFS_ERR_OK : VFS_ERR_IO_ERROR;
}

VfsError create_err(const char* path, uint16_t mode) {
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }
    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];
    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }
    if (!*name) return VFS_ERR_INVALID_ARGS;
    Vnode* parent = resolve(parent_path);
    if (!parent) return VFS_ERR_NOT_FOUND;
    if (!(parent->mode & S_IFDIR)) return VFS_ERR_NOT_DIR;
    if (!parent->ops || !parent->ops->create) return VFS_ERR_NOT_SUPPORTED;
    int result = parent->ops->create(*parent, name, mode);
    return result == 0 ? VFS_ERR_OK : VFS_ERR_IO_ERROR;
}

VfsError unlink_err(const char* path) {
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') slash = p;
    }

    const char* parent_path = "/";
    const char* name = path;
    char parent_buf[MAX_PATH];

    if (slash) {
        size_t parent_len = static_cast<size_t>(slash - path);
        for (size_t i = 0; i < parent_len && i < MAX_PATH - 1; ++i)
            parent_buf[i] = path[i];
        parent_buf[parent_len] = '\0';
        parent_path = parent_buf;
        name = slash + 1;
    }

    if (!*name) return VFS_ERR_INVALID_ARGS;

    Vnode* parent = resolve(parent_path);
    if (!parent) return VFS_ERR_NOT_FOUND;
    if (!(parent->mode & S_IFDIR)) return VFS_ERR_NOT_DIR;
    if (!parent->ops || !parent->ops->unlink) return VFS_ERR_NOT_SUPPORTED;

    int result = parent->ops->unlink(*parent, name);
    return result == 0 ? VFS_ERR_OK : VFS_ERR_IO_ERROR;
}

} // namespace vfs
} // namespace kernel
