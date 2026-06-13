#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/memory/mempool.hpp>
#include <initrd/initrd.hpp>
#include <string.hpp>
#include <utils.hpp>

namespace kernel {
namespace vfs {

struct InitrdFileNode {
    const uint8_t* data;
    uint64_t size;
    Vnode parent;
};

static Vnode initrd_root = {};
static bool root_initialized = false;

static int64_t initrd_file_read(Vnode& self, uint8_t* buffer, uint64_t count, uint64_t offset) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo || !finfo->data) return -1;
    if (offset >= finfo->size) return 0;
    uint64_t avail = finfo->size - offset;
    if (count > avail) count = avail;
    memcpy(buffer, finfo->data + offset, count);
    return static_cast<int64_t>(count);
}

static int64_t initrd_file_write(Vnode&, const uint8_t*, uint64_t, uint64_t) {
    return -1;
}

static int initrd_file_open(Vnode&, uint64_t) {
    return 0;
}

static void initrd_file_close(Vnode& self) {
    if (self.private_data) {
        auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
        finfo->data = nullptr;
        finfo->size = 0;
    }
}

static int64_t initrd_file_lseek(Vnode& self, int64_t offset, int whence, uint64_t* out_pos) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo) return -1;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    case SEEK_END: new_pos = finfo->size + static_cast<uint64_t>(offset); break;
    default: return -1;
    }
    if (new_pos > finfo->size) new_pos = finfo->size;
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}

static int initrd_file_fstat(Vnode& self, VfsStat& st) {
    auto* finfo = static_cast<InitrdFileNode*>(self.private_data);
    if (!finfo) return -1;
    st.st_size = finfo->size;
    st.st_mode = S_IFREG;
    return 0;
}

static int initrd_file_ioctl(Vnode&, uint64_t, void*) {
    return -1;
}

static int initrd_file_readdir(Vnode&, uint64_t&, Dirent&) {
    return -1;
}

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
};

// ── root directory ──

static int64_t initrd_root_read(Vnode&, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t initrd_root_write(Vnode&, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int initrd_root_open(Vnode&, uint64_t) { return 0; }
static void initrd_root_close(Vnode&) {}

static int64_t initrd_root_lseek(Vnode& self, int64_t offset, int whence, uint64_t* out_pos) {
    return initrd_file_lseek(self, offset, whence, out_pos);
}

static int initrd_root_fstat(Vnode&, VfsStat& vfs_stat) {
    vfs_stat.st_size = 0;
    vfs_stat.st_mode = S_IFDIR;
    return 0;
}

static int initrd_root_ioctl(Vnode&, uint64_t, void*) { return -1; }

static int initrd_root_readdir(Vnode&, uint64_t& pos, Dirent& dent) {
    initrd::InitrdEntry entry = {};
    if (!initrd::readdir(&pos, &entry)) return -1;
    size_t idx = 0;
    while (entry.name[idx] && idx < 63) { dent.d_name[idx] = entry.name[idx]; ++idx; }
    dent.d_name[idx] = '\0';
    dent.d_ino = 2;
    return 0;
}

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
};

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
