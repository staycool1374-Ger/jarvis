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

#pragma once

#include <types.hpp>
#include <kernel/vfs/vfs_errors.hpp>

namespace kernel {
namespace vfs {

static constexpr size_t MAX_FDS = CONFIG_MAX_FDS;
static constexpr size_t MAX_MOUNTS = CONFIG_MAX_MOUNTS;
static constexpr size_t MAX_PATH = CONFIG_VFS_MAX_PATH;
static constexpr size_t MAX_PATH_DEPTH = 16;

enum OpenFlags : uint64_t {
    O_RDONLY   = 0,
    O_WRONLY   = 1,
    O_RDWR     = 2,
    O_NONBLOCK = 0x800,
    O_CREAT    = 0x200,
};

enum SeekWhence : int64_t {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2,
};

enum class VfsSentinel : int64_t {
    INVALID_FD = -4,
};
constexpr int64_t VFS_INVALID = static_cast<int64_t>(VfsSentinel::INVALID_FD);

using errors::VfsError;

enum FileMode : uint16_t {
    S_IFREG  = 0x8000,
    S_IFDIR  = 0x4000,
    S_IFCHR  = 0x2000,
};

struct VfsStat {
    uint64_t st_size;
    uint16_t st_mode;
};

struct Dirent {
    char d_name[64];
    uint64_t d_ino;
};

struct Vnode;
struct VnodeOps {
    int64_t (*read)(Vnode& self, uint8_t* buffer, uint64_t count,
        uint64_t offset);
    int64_t (*write)(Vnode& self, const uint8_t* buffer, uint64_t count,
        uint64_t offset);
    int     (*open)(Vnode& self, uint64_t flags);
    void    (*close)(Vnode& self);
    int64_t (*lseek)(Vnode& self, int64_t offset, int whence, uint64_t* out_pos
        );
    int     (*fstat)(Vnode& self, VfsStat& st);
    int     (*ioctl)(Vnode& self, uint64_t request, void* arg);
    int     (*readdir)(Vnode& self, uint64_t& pos, Dirent& dent);
    Vnode*  (*lookup)(Vnode& self, const char* name);
    int     (*mkdir)(Vnode& self, const char* name, uint16_t mode);
    int     (*unlink)(Vnode& self, const char* name);
    int     (*create)(Vnode& self, const char* name, uint16_t mode);
};

struct Vnode {
    const VnodeOps* ops;
    uint64_t ino;
    uint64_t size;
    uint16_t mode;
    void* private_data;
    uint64_t refcount;
};

struct FileDescription {
    Vnode* vnode;
    uint64_t offset;
    uint64_t flags;
    bool used;
};

struct FdTable {
    FileDescription fds[MAX_FDS];

    /// @brief Allocate a file descriptor entry.
    /// @return The fd index, or VFS_INVALID if full.
    int alloc();
    /// @brief Allocate a file descriptor entry with error code.
    /// @param[out] out_fd The allocated fd on success.
    /// @return VfsError code.
    VfsError alloc_err(int& out_fd);
    /// @brief Release a file descriptor entry.
    void free(int file_descriptor);
    /// @brief Release a file descriptor entry with error code.
    /// @return VfsError code.
    VfsError free_err(int file_descriptor);
    /// @brief Look up a file descriptor by index.
    /// @return Pointer to the entry, or nullptr if invalid.
    FileDescription* get(int file_descriptor);
    /// @brief Look up a file descriptor by index with error code.
    /// @param[out] out_fd Pointer to the entry on success.
    /// @return VfsError code.
    VfsError get_err(int file_descriptor, FileDescription*& out_fd);
};

struct Filesystem {
    const char* name;
    Vnode* (*get_root)();
};

struct Mount {
    const char* mount_point;
    Filesystem* fs;
    Vnode* root_vnode;
    bool used;
};

/// @brief Resolve an absolute path to a vnode.
/// @return The vnode, or nullptr if not found.
Vnode* resolve(const char* path);
/// @brief Resolve an absolute path to a vnode with error code.
/// @param[out] out_vnode The resolved vnode on success.
/// @return VfsError code.
VfsError resolve_err(const char* path, Vnode*& out_vnode);
/// @brief Mount a filesystem at a given mount point.
/// @return 0 on success, or VFS_INVALID on failure.
int mount(Filesystem& filesystem, const char* mount_point);
/// @brief Mount a filesystem at a given mount point with error code.
/// @return VfsError code.
VfsError mount_err(Filesystem& filesystem, const char* mount_point);
/// @brief Initialize the VFS subsystem (root vnode, mounts, etc.).
void init();
/// @brief Initialize the VFS subsystem with error code.
/// @return VfsError code.
VfsError init_err();
/// @brief Reset VFS globals and re-mount standard filesystems.
/// Called from snapshot_restore() after MemPool/PMM restoration to
/// clear stale mount-table entries and vnode pointers that survived
/// test execution.
void reset_and_remount();
/// @brief Find a registered filesystem driver by name.
/// @return The filesystem, or nullptr if not found.
Filesystem* find_fs(const char* name);
/// @brief Find a registered filesystem driver by name with error code.
/// @param[out] out_fs The filesystem on success.
/// @return VfsError code.
VfsError find_fs_err(const char* name, Filesystem*& out_fs);
/// @brief Get the root vnode of the VFS tree.
Vnode* get_root_vnode();
/// @brief Set the root vnode of the VFS tree.
void set_root_vnode(Vnode& vnode);
/// @brief Set the root vnode of the VFS tree with error code.
/// @return VfsError code.
VfsError set_root_vnode_err(Vnode& vnode);

/// @brief Create a subdirectory at the given path.
/// @return 0 on success, VFS_INVALID on failure.
int mkdir(const char* path, uint16_t mode);
/// @brief Create a subdirectory at the given path with error code.
/// @return VfsError code.
VfsError mkdir_err(const char* path, uint16_t mode);

/// @brief Create a regular file at the given path.
/// @return 0 on success, VFS_INVALID on failure.
int create(const char* path, uint16_t mode);
/// @brief Create a regular file at the given path with error code.
/// @return VfsError code.
VfsError create_err(const char* path, uint16_t mode);

/// @brief Remove a file or empty directory at the given path.
/// @return 0 on success, VFS_INVALID on failure.
int unlink(const char* path);
/// @brief Remove a file or empty directory at the given path with error code.
/// @return VfsError code.
VfsError unlink_err(const char* path);

} // namespace vfs
} // namespace kernel
