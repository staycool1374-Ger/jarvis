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
    Vnode* parent;
};

fat32::Fat32Partition* fat32_partition_instance = nullptr;

// Forward declarations
extern Vnode fat32_root_vnode;
static Vnode* fat32_dir_lookup(Vnode& self, const char* name);
static Vnode* fat32_file_lookup(Vnode&, const char*);
static int fat32_dir_mkdir(Vnode& self, const char* name, uint16_t mode);
static int fat32_dir_unlink(Vnode& self, const char* name);

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
    nullptr,
    nullptr,
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
    if (&self == &fat32_root_vnode) {
        self.refcount = 1;
        return;
    }
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
    fat32_dir_mkdir,
    fat32_dir_unlink,
};

// -------------------------------------------------------------------
// mkdir / unlink
// -------------------------------------------------------------------

static int fat32_dir_mkdir(Vnode& self, const char* name, uint16_t) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data || !data->fs) return VFS_INVALID;

    // Check if entry already exists
    fat32::DirEntry existing;
    if (fat32::lookup_in_dir(*data->fs, data->cluster, name, existing))
        return VFS_INVALID; // already exists

    // Allocate a cluster for the new directory
    uint32_t new_cluster = 0;
    if (!data->fs->alloc_cluster(0, new_cluster)) return VFS_INVALID;

    // Write "." entry pointing to self
    fat32::DirEntryRaw dot_entry;
    __builtin_memset(&dot_entry, 0, sizeof(dot_entry));
    fat32::name_to_short_name(".", dot_entry.name);
    dot_entry.attrs = fat32::ATTR_DIRECTORY;
    dot_entry.cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
    dot_entry.cluster_high = static_cast<uint16_t>((new_cluster >> 16) & 0xFFFF);

    // Write ".." entry pointing to parent
    fat32::DirEntryRaw dotdot_entry;
    __builtin_memset(&dotdot_entry, 0, sizeof(dotdot_entry));
    fat32::name_to_short_name("..", dotdot_entry.name);
    dotdot_entry.attrs = fat32::ATTR_DIRECTORY;
    uint32_t parent_cluster = data->cluster;
    dotdot_entry.cluster_low = static_cast<uint16_t>(parent_cluster & 0xFFFF);
    dotdot_entry.cluster_high = static_cast<uint16_t>((parent_cluster >> 16) & 0xFFFF);

    // Write entries into the new directory's first data area
    uint32_t cluster_size = data->fs->bpb().sectors_per_cluster *
                            data->fs->bpb().bytes_per_sector;
    uint8_t dir_data[32 * 1024];
    __builtin_memset(dir_data, 0, cluster_size);
    __builtin_memcpy(dir_data, &dot_entry, sizeof(dot_entry));
    __builtin_memcpy(dir_data + 32, &dotdot_entry, sizeof(dotdot_entry));
    if (!data->fs->write_cluster(new_cluster, dir_data)) {
        fat32::free_cluster_chain(*data->fs, new_cluster);
        return VFS_INVALID;
    }

    // Add entry for new directory in the parent
    fat32::DirEntryRaw new_entry;
    __builtin_memset(&new_entry, 0, sizeof(new_entry));
    fat32::name_to_short_name(name, new_entry.name);
    new_entry.attrs = fat32::ATTR_DIRECTORY;
    new_entry.cluster_low = static_cast<uint16_t>(new_cluster & 0xFFFF);
    new_entry.cluster_high = static_cast<uint16_t>((new_cluster >> 16) & 0xFFFF);

    if (!fat32::add_dir_entry(*data->fs, data->cluster, new_entry)) {
        fat32::free_cluster_chain(*data->fs, new_cluster);
        return VFS_INVALID;
    }

    return 0;
}

static int fat32_dir_unlink(Vnode& self, const char* name) {
    auto* data = static_cast<Fat32VnodeData*>(self.private_data);
    if (!data || !data->fs) return VFS_INVALID;

    fat32::DirEntry entry;
    if (!fat32::lookup_in_dir(*data->fs, data->cluster, name, entry))
        return VFS_INVALID;

    // If it's a directory, check that it is empty
    if (entry.is_directory && entry.cluster != 0) {
        // Count entries in the target directory (skip . and ..)
        uint64_t count_pos = 0;
        fat32::DirEntry child_entry;
        uint64_t real_entries = 0;
        while (fat32::read_dir_entry(*data->fs, entry.cluster,
                                      count_pos, child_entry)) {
            if (child_entry.valid) ++real_entries;
        }
        // A new directory has only . and .. — so real_entries should be 0
        if (real_entries > 0) return VFS_INVALID; // not empty

        // Free the directory's cluster chain
        fat32::free_cluster_chain(*data->fs, entry.cluster);
    } else if (!entry.is_directory && entry.cluster != 0) {
        // Free file's cluster chain
        fat32::free_cluster_chain(*data->fs, entry.cluster);
    }

    // Remove the entry from the parent
    if (!fat32::remove_dir_entry(*data->fs, data->cluster, name))
        return VFS_INVALID;

    return 0;
}

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
    vdata->parent = &self;

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

Vnode fat32_root_vnode = {};
static Fat32VnodeData fat32_root_vdata;
static bool root_initialized = false;

static Vnode* fat32_get_root() {
    if (!fat32_partition_instance || !fat32_partition_instance->bpb().valid)
        return nullptr;

    if (!root_initialized ||
         fat32_root_vdata.fs != fat32_partition_instance) {
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
