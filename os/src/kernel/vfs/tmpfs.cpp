#include <kernel/vfs/tmpfs.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <constants.hpp>

namespace kernel {
namespace vfs {

struct TmpfsEntry {
    char name[64];
    Vnode* vnode;
    TmpfsEntry* next;
};

static Vnode tmpfs_root;

static uint64_t next_ino = 1;

static TmpfsEntry* find_entry(Vnode& dir, const char* name) {
    auto* e = static_cast<TmpfsEntry*>(dir.private_data);
    while (e) {
        if (strcmp(e->name, name) == 0) return e;
        e = e->next;
    }
    return nullptr;
}

static int64_t tmpfs_file_read(Vnode& self, uint8_t* buffer, uint64_t count, uint64_t offset) {
    arch::IrqGuard guard;
    if (offset >= self.size) return 0;
    uint64_t avail = self.size - offset;
    if (count > avail) count = avail;
    uint64_t phys = reinterpret_cast<uint64_t>(self.private_data);
    if (!phys) return 0;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(arch::HHDM_OFFSET + phys) + offset;
    __builtin_memcpy(buffer, src, count);
    return static_cast<int64_t>(count);
}

static int64_t tmpfs_file_write(Vnode& self, const uint8_t* buffer, uint64_t count, uint64_t offset) {
    arch::IrqGuard guard;
    uint64_t needed = offset + count;
    if (needed > 64_KiB) return VFS_INVALID; // per-file limit

    // Need to allocate pages if not already
    uint64_t phys = reinterpret_cast<uint64_t>(self.private_data);
    if (!phys) {
        phys = PMM::alloc_user_contiguous(16); // 16 pages = 64 KiB max
        if (!phys) return VFS_INVALID;
        self.private_data = reinterpret_cast<void*>(phys);
        __builtin_memset(reinterpret_cast<void*>(arch::HHDM_OFFSET + phys), 0, 64_KiB);
    }

    uint8_t* dst = reinterpret_cast<uint8_t*>(arch::HHDM_OFFSET + phys) + offset;
    __builtin_memcpy(dst, buffer, count);
    if (needed > self.size) self.size = needed;
    return static_cast<int64_t>(count);
}

static int tmpfs_file_open(Vnode&, uint64_t) { return 0; }
static void tmpfs_file_close(Vnode&) {}

static int tmpfs_fstat(Vnode& self, VfsStat& st) {
    st.st_size = self.size;
    st.st_mode = self.mode;
    return 0;
}

static int64_t tmpfs_file_lseek(Vnode& self, int64_t offset, int whence, uint64_t* out_pos) {
    (void)self;
    (void)offset;
    (void)whence;
    (void)out_pos;
    return 0;
}

static int tmpfs_readdir(Vnode& self, uint64_t& pos, Dirent& dent) {
    arch::IrqGuard guard;
    uint64_t idx = 0;
    auto* e = static_cast<TmpfsEntry*>(self.private_data);
    while (e) {
        if (idx == pos) {
            size_t i = 0;
            while (e->name[i] && i < sizeof(dent.d_name) - 1) {
                dent.d_name[i] = e->name[i];
                ++i;
            }
            dent.d_name[i] = '\0';
            dent.d_ino = e->vnode->ino;
            ++pos;
            return 0;
        }
        ++idx;
        e = e->next;
    }
    return VFS_INVALID;
}

static int tmpfs_unlink(Vnode& self, const char* name);

static int tmpfs_create(Vnode& self, const char* name, uint16_t mode);

static Vnode* tmpfs_lookup(Vnode& self, const char* name) {
    arch::IrqGuard guard;
    auto* e = find_entry(self, name);
    return e ? e->vnode : nullptr;
}

static int tmpfs_mkdir(Vnode& self, const char* name, uint16_t) {
    arch::IrqGuard guard;
    if (find_entry(self, name)) return VFS_INVALID;
    auto* entry = static_cast<TmpfsEntry*>(MemPool::alloc(sizeof(TmpfsEntry)));
    if (!entry) return VFS_INVALID;
    auto* vn = static_cast<Vnode*>(MemPool::alloc(sizeof(Vnode)));
    if (!vn) { MemPool::free(entry); return VFS_INVALID; }

    size_t i = 0;
    while (name[i] && i < sizeof(entry->name) - 1) { entry->name[i] = name[i]; ++i; }
    entry->name[i] = '\0';

    static const VnodeOps dir_ops = {
        nullptr,         // read
        nullptr,         // write
        nullptr,         // open
        nullptr,         // close
        nullptr,         // lseek
        tmpfs_fstat,
        nullptr,         // ioctl
        tmpfs_readdir,
        tmpfs_lookup,
        tmpfs_mkdir,
        nullptr,         // unlink — handled at parent level
        tmpfs_create,
    };

    vn->ops = &dir_ops;
    vn->ino = next_ino++;
    vn->size = 0;
    vn->mode = S_IFDIR;
    vn->private_data = nullptr;
    vn->refcount = 0;

    entry->vnode = vn;
    entry->next = static_cast<TmpfsEntry*>(self.private_data);
    self.private_data = entry;
    return 0;
}

static int tmpfs_unlink(Vnode& self, const char* name) {
    arch::IrqGuard guard;
    auto* prev = static_cast<TmpfsEntry*>(nullptr);
    auto* e = static_cast<TmpfsEntry*>(self.private_data);
    while (e) {
        if (strcmp(e->name, name) == 0) {
            if (e->vnode->mode & S_IFDIR) {
                if (e->vnode->private_data) return VFS_INVALID;
            }
            if (prev) prev->next = e->next;
            else self.private_data = e->next;
            if (e->vnode->private_data && !(e->vnode->mode & S_IFDIR)) {
                uint64_t phys = reinterpret_cast<uint64_t>(e->vnode->private_data);
                if (phys) PMM::free_page(phys);
            }
            MemPool::free(e->vnode);
            MemPool::free(e);
            return 0;
        }
        prev = e;
        e = e->next;
    }
    return VFS_INVALID;
}

static const VnodeOps tmpfs_file_ops = {
    tmpfs_file_read,
    tmpfs_file_write,
    tmpfs_file_open,
    tmpfs_file_close,
    tmpfs_file_lseek,
    tmpfs_fstat,
    nullptr,         // ioctl
    nullptr,         // readdir
    nullptr,         // lookup
    nullptr,         // mkdir
    nullptr,         // unlink
    nullptr,         // create
};

static int tmpfs_create(Vnode& self, const char* name, uint16_t) {
    arch::IrqGuard guard;
    if (find_entry(self, name)) return VFS_INVALID;
    auto* entry = static_cast<TmpfsEntry*>(MemPool::alloc(sizeof(TmpfsEntry)));
    if (!entry) return VFS_INVALID;
    auto* vn = static_cast<Vnode*>(MemPool::alloc(sizeof(Vnode)));
    if (!vn) { MemPool::free(entry); return VFS_INVALID; }

    size_t i = 0;
    while (name[i] && i < sizeof(entry->name) - 1) { entry->name[i] = name[i]; ++i; }
    entry->name[i] = '\0';

    vn->ops = &tmpfs_file_ops;
    vn->ino = next_ino++;
    vn->size = 0;
    vn->mode = S_IFREG;
    vn->private_data = nullptr;
    vn->refcount = 0;

    entry->vnode = vn;
    entry->next = static_cast<TmpfsEntry*>(self.private_data);
    self.private_data = entry;
    return 0;
}

static Vnode* tmpfs_get_root() {
    static bool inited = false;
    if (!inited) {
        static const VnodeOps root_ops = {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            tmpfs_fstat,
            nullptr,
            tmpfs_readdir,
            tmpfs_lookup,
            tmpfs_mkdir,
            tmpfs_unlink,
            tmpfs_create,
        };
        tmpfs_root.ops = &root_ops;
        tmpfs_root.ino = next_ino++;
        tmpfs_root.size = 0;
        tmpfs_root.mode = S_IFDIR;
        tmpfs_root.private_data = nullptr;
        tmpfs_root.refcount = 0;
        inited = true;
    }
    return &tmpfs_root;
}

void tmpfs_reset_root() {
    tmpfs_root.private_data = nullptr;
}

Filesystem tmpfs_fs = {
    "tmpfs",
    tmpfs_get_root,
};

} // namespace vfs
} // namespace kernel
