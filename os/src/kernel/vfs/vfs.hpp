#pragma once

#include <types.hpp>

namespace kernel {
namespace vfs {

static constexpr size_t MAX_FDS = 32;
static constexpr size_t MAX_MOUNTS = 8;
static constexpr size_t MAX_PATH = 256;
static constexpr size_t MAX_PATH_DEPTH = 16;

enum OpenFlags : uint64_t {
    O_RDONLY   = 0,
    O_WRONLY   = 1,
    O_RDWR     = 2,
    O_NONBLOCK = 0x800,
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
};

struct Vnode {
    const VnodeOps* ops;
    uint64_t ino;
    uint64_t size;
    uint16_t mode;
    void* private_data;
    uint64_t refcount;

    Vnode()
        : ops(nullptr)
        , ino(0)
        , size(0)
        , mode(0)
        , private_data(nullptr)
        , refcount(0)
        {}

    Vnode(const VnodeOps* ops_, uint64_t ino_, uint64_t size_, uint16_t mode_,
        void* private_data_)
        : ops(ops_)
        , ino(ino_)
        , size(size_)
        , mode(mode_)
        , private_data(private_data_)
        , refcount(0)
        {}
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
    /// @brief Release a file descriptor entry.
    void free(int file_descriptor);
    /// @brief Look up a file descriptor by index.
    /// @return Pointer to the entry, or nullptr if invalid.
    FileDescription* get(int file_descriptor);
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
/// @brief Mount a filesystem at a given mount point.
/// @return 0 on success, or VFS_INVALID on failure.
int mount(Filesystem& filesystem, const char* mount_point);
/// @brief Initialize the VFS subsystem (root vnode, mounts, etc.).
void init();
/// @brief Find a registered filesystem driver by name.
/// @return The filesystem, or nullptr if not found.
Filesystem* find_fs(const char* name);
/// @brief Get the root vnode of the VFS tree.
Vnode* get_root_vnode();
/// @brief Set the root vnode of the VFS tree.
void set_root_vnode(Vnode& vnode);

} // namespace vfs
} // namespace kernel
