#include <kernel/vfs/fat32_fs.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/test/resource_tracker.hpp>
#include <string.hpp>

namespace kernel {
namespace vfs {

struct Fat32VnodeData {
    fat32::Fat32Partition* fs;
    uint32_t cluster;
    uint32_t size;
};

fat32::Fat32Partition* fat32_partition_instance = nullptr;

// Forward declarations
static Vnode* fat32_dir_lookup(Vnode& self, const char* name);
static Vnode* fat32_file_lookup(Vnode&, const char*);

// -------------------------------------------------------------------
// File operations
// -------------------------------------------------------------------

static int64_t fat32_file_read(Vnode& self, uint8_t* buffer, uint64_t count,
                                uint64_t offset) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data || !data->fs) return VFS_INVALID;
    return fat32::read_file(*data->fs, data->cluster, data->size,
                             offset, count, buffer);
}

static int64_t fat32_file_write(Vnode&, const uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID;
}

static int fat32_file_open(Vnode&, uint64_t) {
    return 0;
}

static void fat32_file_close(Vnode& self) {
    if (self.private_data) {
        auto* data = static_cast<Fat32VnodeData*>(self.private_data);
        MemPool::free(data);
        self.private_data = nullptr;
    }
    test::ResourceTracker::instance().track_vnode_remove();
    MemPool::free(&self);
}

static int64_t fat32_file_lseek(Vnode& self, int64_t offset, int whence,
                                 uint64_t* out_pos) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data) return VFS_INVALID;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    case SEEK_END: new_pos = data->size + static_cast<uint64_t>(offset); break;
    default: return VFS_INVALID;
    }
    if (new_pos > data->size) new_pos = data->size;
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}

static int fat32_file_fstat(Vnode& self, VfsStat& st) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data) return VFS_INVALID;
    st.st_size = data->size;
    st.st_mode = S_IFREG;
    return 0;
}

static int fat32_file_ioctl(Vnode&, uint64_t, void*) {
    return VFS_INVALID;
}

static int fat32_file_readdir(Vnode&, uint64_t&, Dirent&) {
    return VFS_INVALID;
}

static Vnode* fat32_file_lookup(Vnode&, const char*) {
    return nullptr;
}

static const VnodeOps fat32_file_ops = {
    fat32_file_read,
    fat32_file_write,
    fat32_file_open,
    fat32_file_close,
    fat32_file_lseek,
    fat32_file_fstat,
    fat32_file_ioctl,
    fat32_file_readdir,
    fat32_file_lookup,
};

// -------------------------------------------------------------------
// Directory operations
// -------------------------------------------------------------------

static int64_t fat32_dir_read(Vnode&, uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID;
}

static int64_t fat32_dir_write(Vnode&, const uint8_t*, uint64_t, uint64_t) {
    return VFS_INVALID;
}

static int fat32_dir_open(Vnode&, uint64_t) {
    return 0;
}

static void fat32_dir_close(Vnode& self) {
    if (self.private_data) {
        auto* data = static_cast<Fat32VnodeData*>(self.private_data);
        MemPool::free(data);
        self.private_data = nullptr;
    }
    test::ResourceTracker::instance().track_vnode_remove();
    MemPool::free(&self);
}

static int64_t fat32_dir_lseek(Vnode& self, int64_t offset, int whence,
                                uint64_t* out_pos) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data) return VFS_INVALID;
    uint64_t new_pos = 0;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: new_pos = *out_pos + static_cast<uint64_t>(offset); break;
    case SEEK_END: return VFS_INVALID;
    default: return VFS_INVALID;
    }
    *out_pos = new_pos;
    return static_cast<int64_t>(new_pos);
}

static int fat32_dir_fstat(Vnode&, VfsStat& st) {
    st.st_size = 0;
    st.st_mode = S_IFDIR;
    return 0;
}

static int fat32_dir_ioctl(Vnode&, uint64_t, void*) {
    return VFS_INVALID;
}

static int fat32_dir_readdir(Vnode& self, uint64_t& pos, Dirent& dent) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data || !data->fs) return VFS_INVALID;

    fat32::DirEntry entry = {};
    if (!fat32::read_dir_entry(*data->fs, data->cluster, pos, entry))
        return VFS_INVALID;

    size_t idx = 0;
    while (entry.name[idx] && idx < 63) {
        dent.d_name[idx] = entry.name[idx];
        ++idx;
    }
    dent.d_name[idx] = '\0';
    dent.d_ino = 1;
    return 0;
}

static const VnodeOps fat32_dir_ops = {
    fat32_dir_read,
    fat32_dir_write,
    fat32_dir_open,
    fat32_dir_close,
    fat32_dir_lseek,
    fat32_dir_fstat,
    fat32_dir_ioctl,
    fat32_dir_readdir,
    fat32_dir_lookup,
};

static Vnode* fat32_dir_lookup(Vnode& self, const char* name) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data || !data->fs) return nullptr;

    fat32::DirEntry entry = {};
    if (!fat32::lookup_in_dir(*data->fs, data->cluster, name, entry))
        return nullptr;

    auto* vdata = static_cast<Fat32VnodeData*>(
        MemPool::alloc(sizeof(Fat32VnodeData)));
    if (!vdata) return nullptr;
    vdata->fs = data->fs;
    vdata->cluster = entry.cluster;
    vdata->size = entry.size;

    auto* vnode = static_cast<Vnode*>(MemPool::alloc(sizeof(Vnode)));
    if (!vnode) {
        MemPool::free(vdata);
        return nullptr;
    }

    test::ResourceTracker::instance().track_vnode_add();
    vnode->ops = entry.is_directory ? &fat32_dir_ops : &fat32_file_ops;
    vnode->ino = 1;
    vnode->size = entry.size;
    vnode->mode = entry.is_directory ? S_IFDIR : S_IFREG;
    vnode->private_data = vdata;
    vnode->refcount = 1;
    return vnode;
}

// -------------------------------------------------------------------
// Filesystem root
// -------------------------------------------------------------------

static Vnode fat32_root_vnode = {};
static Fat32VnodeData fat32_root_vdata;
static bool root_initialized = false;

static Vnode* fat32_get_root() {
    if (!fat32_partition_instance || !fat32_partition_instance->bpb().valid)
        return nullptr;

    if (!root_initialized) {
        fat32_root_vdata.fs = fat32_partition_instance;
        fat32_root_vdata.cluster = fat32_partition_instance->bpb().root_cluster;
        fat32_root_vdata.size = 0;

        fat32_root_vnode.ops = &fat32_dir_ops;
        fat32_root_vnode.ino = 0;
        fat32_root_vnode.size = 0;
        fat32_root_vnode.mode = S_IFDIR;
        fat32_root_vnode.private_data = &fat32_root_vdata;
        fat32_root_vnode.refcount = 1;
        root_initialized = true;
    }
    return &fat32_root_vnode;
}

Filesystem fat32_fs = {
    "fat32",
    fat32_get_root,
};

int mount_fat32(const char* mount_point) {
    if (!fat32_partition_instance ||
        !fat32_partition_instance->bpb().valid)
        return VFS_INVALID;
    return mount(fat32_fs, mount_point);
}

} // namespace vfs
} // namespace kernel
