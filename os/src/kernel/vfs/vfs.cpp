#include <kernel/vfs/vfs.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <string.hpp>
#include <utils.hpp>

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
            return static_cast<int>(i);
        }
    }
    return -1;
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
    fds[file_descriptor].used = false;
    fds[file_descriptor].vnode = nullptr;
    fds[file_descriptor].offset = 0;
    fds[file_descriptor].flags = 0;
}

FileDescription* FdTable::get(int file_descriptor) {
    if (file_descriptor < 0 || static_cast<size_t>(file_descriptor) >= MAX_FDS
        ) return nullptr;
    if (!fds[file_descriptor].used) return nullptr;
    return &fds[file_descriptor];
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
                if (pv) current = pv;
            }
            continue;
        }

        Vnode* child = current->ops ? current->ops->lookup(*current, comp) :
            nullptr;
        if (!child) return nullptr;
        current = child;
    }

    return current;
}

int mount(Filesystem& filesystem, const char* mount_point) {
    if (!filesystem.get_root || mount_count >= MAX_MOUNTS) return -1;

    Vnode* root = filesystem.get_root();
    if (!root) return -1;

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

} // namespace vfs
} // namespace kernel
