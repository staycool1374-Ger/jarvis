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
    int64_t (*read)(Vnode* self, uint8_t* buf, uint64_t count, uint64_t offset);
    int64_t (*write)(Vnode* self, const uint8_t* buf, uint64_t count, uint64_t offset);
    int     (*open)(Vnode* self, uint64_t flags);
    void    (*close)(Vnode* self);
    int64_t (*lseek)(Vnode* self, int64_t offset, int whence, uint64_t* out_pos);
    int     (*fstat)(Vnode* self, VfsStat* st);
    int     (*ioctl)(Vnode* self, uint64_t request, void* arg);
    int     (*readdir)(Vnode* self, uint64_t* pos, Dirent* dent);
    Vnode*  (*lookup)(Vnode* self, const char* name);
};

struct Vnode {
    const VnodeOps* ops;
    uint64_t ino;
    uint64_t size;
    uint16_t mode;
    void* private_data;
    int refcount = 1;
};

struct FileDescription {
    Vnode* vnode;
    uint64_t offset;
    uint64_t flags;
    bool used;
};

struct FdTable {
    FileDescription fds[MAX_FDS];

    int alloc();
    void free(int fd);
    FileDescription* get(int fd);
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

Vnode* resolve(const char* path);
int mount(Filesystem* fs, const char* mount_point);
void init();
Filesystem* find_fs(const char* name);
Vnode* get_root_vnode();
void set_root_vnode(Vnode* vn);

} // namespace vfs
} // namespace kernel
