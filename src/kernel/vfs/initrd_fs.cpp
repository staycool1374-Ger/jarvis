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

/// @file initrd_fs.cpp
/// @brief Initial ramdisk filesystem implementation (read-only file vnodes).

#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <initrd/initrd.hpp>
#include <string.hpp>
#include <utils.hpp>

namespace kernel {
namespace vfs {

/// @brief Per-vnode private data for initrd file vnodes.
struct InitrdFileNode {
    const uint8_t* data;  ///< Pointer to file data in the initrd image.
    uint64_t size;        ///< File size in bytes.
    Vnode parent;         ///< Parent directory vnode.
};

static Vnode initrd_root = {};
static bool root_initialized = false;

/// @brief Read data from an initrd file vnode.
static int64_t initrd_file_read(Vnode& self, uint8_t* buffer, uint64_t count,
    uint64_t offset) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo || !finfo->data) return VFS_INVALID;
    if (offset >= finfo->size) return 0;
    uint64_t avail = finfo->size - offset;
    if (count > avail) count = avail;
    memcpy(buffer, finfo->data + offset, count);
    return static_cast<int64_t>(count);
}

/// @brief Write to an initrd file (not supported, read-only).
static int64_t initrd_file_write(Vnode&, const uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID;
}

/// @brief Open an initrd file vnode.
static int initrd_file_open(Vnode&, uint64_t) {
    return 0;
}

/// @brief Close an initrd file vnode, freeing private data.
static void initrd_file_close(Vnode& self) {
    if (self.private_data) {
        auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
        kernel::MemPool::free(finfo);
        self.private_data = nullptr;
    }
    kernel::test::ResourceTracker::instance().track_vnode_remove();
    kernel::MemPool::free(&self);
}

/// @brief Seek within an initrd file.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int64_t initrd_file_lseek(Vnode& self, int64_t offset, int whence,
    uint64_t* out_pos) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo) return VFS_INVALID;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    case SEEK_END: new_pos = finfo->size + static_cast<uint64_t>(offset); break;
    default: return VFS_INVALID;
    }
    if (new_pos > finfo->size) new_pos = finfo->size;
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}

/// @brief Get initrd file status.
static int initrd_file_fstat(Vnode& self, VfsStat& st) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo) return VFS_INVALID;
    st.st_size = finfo->size;
    st.st_mode = S_IFREG;
    return 0;
}

/// @brief I/O control on initrd file (not supported).
static int initrd_file_ioctl(Vnode&, uint64_t, void*) {
    return VFS_INVALID;
}

/// @brief Read directory on initrd file (not supported).
static int initrd_file_readdir(Vnode&, uint64_t&, Dirent&) {
    return VFS_INVALID;
}

/// @brief Look up child in initrd file (not supported).
static Vnode* initrd_file_lookup(Vnode&, const char*) {
    return nullptr;
}

static const VnodeOps initrd_file_ops = {
    initrd_file_read,
    initrd_file_write,
    initrd_file_open,
    initrd_file_close,
    initrd_file_lseek,
    initrd_file_fstat,
    initrd_file_ioctl,
    initrd_file_readdir,
    initrd_file_lookup,
    nullptr,
    nullptr,
    nullptr,         // create
};

// ── root directory ──

/// @brief Read from initrd root (not supported).
static int64_t initrd_root_read(Vnode&, uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID; }
/// @brief Write to initrd root (not supported).
static int64_t initrd_root_write(Vnode&, const uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID; }
/// @brief Open the initrd root.
static int initrd_root_open(Vnode&, uint64_t) { return 0; }
/// @brief Close the initrd root.
static void initrd_root_close(Vnode&) {}

/// @brief Seek within initrd root (delegates to file lseek).
static int64_t initrd_root_lseek(Vnode& self, int64_t offset, int whence,
    uint64_t* out_pos) {
    return initrd_file_lseek(self, offset, whence, out_pos);
}

/// @brief Get initrd root status.
static int initrd_root_fstat(Vnode&, VfsStat& vfs_stat) {
    vfs_stat.st_size = 0;
    vfs_stat.st_mode = S_IFDIR;
    return 0;
}

/// @brief I/O control on initrd root (not supported).
static int initrd_root_ioctl(Vnode&, uint64_t, void*) { return VFS_INVALID; }

/// @brief Read a directory entry from initrd root.
static int initrd_root_readdir(Vnode&, uint64_t& pos, Dirent& dent) {
    initrd::InitrdEntry entry = {};
    if (!initrd::readdir(&pos, &entry)) return VFS_INVALID;
    size_t idx = 0;
    while (entry.name[idx] && idx < 63) { dent.d_name[idx] = entry.name[idx
        ]; ++idx; }
    dent.d_name[idx] = '\0';
    dent.d_ino = 2;
    return 0;
}

/// @brief Look up a file by name in the initrd root directory.
static Vnode* initrd_root_lookup(Vnode&, const char* name) {
    initrd::InitrdFile file = initrd::find(name);
    if (!file.data) return nullptr;

    auto* finfo = static_cast<InitrdFileNode*>(
        kernel::MemPool::alloc(sizeof(InitrdFileNode)));
    if (!finfo) return nullptr;
    finfo->data = file.data;
    finfo->size = file.size;
    finfo->parent = initrd_root;

    auto* vnode = static_cast<Vnode*>(
        kernel::MemPool::alloc(sizeof(Vnode)));
    if (!vnode) {
        kernel::MemPool::free(finfo);
        return nullptr;
    }
    kernel::test::ResourceTracker::instance().track_vnode_add();
    vnode->ops = &initrd_file_ops;
    vnode->ino = 1;
    vnode->size = file.size;
    vnode->mode = S_IFREG;
    vnode->private_data = finfo;
    vnode->refcount = 1;
    return vnode;
}

static const VnodeOps initrd_root_ops = {
    initrd_root_read,
    initrd_root_write,
    initrd_root_open,
    initrd_root_close,
    initrd_root_lseek,
    initrd_root_fstat,
    initrd_root_ioctl,
    initrd_root_readdir,
    initrd_root_lookup,
    nullptr,
    nullptr,
    nullptr,         // create
};

/// @brief Get the initrd root vnode (lazily initialised).
static Vnode* initrd_get_root() {
    if (!root_initialized) {
        initrd_root.ops = &initrd_root_ops;
        initrd_root.ino = 0;
        initrd_root.size = 0;
        initrd_root.mode = S_IFDIR;
        initrd_root.private_data = nullptr;
        root_initialized = true;
    }
    return &initrd_root;
}

Filesystem initrd_fs = {
    "initrd",
    initrd_get_root,
};

} // namespace vfs
} // namespace kernel
