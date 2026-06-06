#include <kernel/vfs/devfs.hpp>
#include <services/terminal/terminal.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/keyboard.hpp>
#include <string.hpp>

namespace kernel {
namespace vfs {

// ── serial helpers (COM1) ──
[[gnu::always_inline]] static inline bool serial_has_data() {
    return arch::inb(0x3F8 + 5) & 1;
}

void devfs_init() {
}

// ── tty vnode (serial console) ──
static int64_t tty_read(Vnode* self, uint8_t* buf, uint64_t count, uint64_t) {
    if (count == 0) return 0;
    for (;;) {
        if (serial_has_data()) {
            char c = arch::inb(0x3F8);
            buf[0] = (c == '\r') ? '\n' : c;
            return 1;
        }
        if (self->private_data && (reinterpret_cast<uint64_t>(self->private_data) & O_NONBLOCK)) {
            return -1;
        }
        asm volatile("pause");
    }
}

static int64_t tty_write(Vnode*, const uint8_t* buf, uint64_t count, uint64_t) {
    service::Terminal::write(reinterpret_cast<const char*>(buf), count);
    return static_cast<int64_t>(count);
}

static int tty_open(Vnode* self, uint64_t flags) {
    self->private_data = reinterpret_cast<void*>(flags);
    return 0;
}

static void tty_close(Vnode*) {}

static int64_t tty_lseek(Vnode*, int64_t, int, uint64_t*) { return -1; }
static int tty_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFCHR;
    return 0;
}
static int tty_ioctl(Vnode*, uint64_t, void*) { return -1; }
static int tty_readdir(Vnode*, uint64_t*, Dirent*) { return -1; }
static Vnode* tty_lookup(Vnode*, const char*) { return nullptr; }

static const VnodeOps tty_ops = {
    tty_read, tty_write, tty_open, tty_close,
    tty_lseek, tty_fstat, tty_ioctl, tty_readdir, tty_lookup,
};

static Vnode tty_vnode = {
    &tty_ops, 1, 0, S_IFCHR, nullptr,
};

// ── null vnode ──
static int64_t null_read(Vnode*, uint8_t*, uint64_t, uint64_t) { return 0; }
static int64_t null_write(Vnode*, const uint8_t*, uint64_t count, uint64_t) {
    return static_cast<int64_t>(count);
}
static int null_open(Vnode*, uint64_t) { return 0; }
static void null_close(Vnode*) {}
static int64_t null_lseek(Vnode*, int64_t offset, int whence, uint64_t* out_pos) {
    switch (whence) {
    case SEEK_SET: *out_pos = static_cast<uint64_t>(offset); break;
    case SEEK_CUR: *out_pos += static_cast<uint64_t>(offset); break;
    case SEEK_END: *out_pos = static_cast<uint64_t>(offset); break;
    }
    return static_cast<int64_t>(*out_pos);
}
static int null_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFCHR;
    return 0;
}
static int null_ioctl(Vnode*, uint64_t, void*) { return -1; }
static int null_readdir(Vnode*, uint64_t*, Dirent*) { return -1; }
static Vnode* null_lookup(Vnode*, const char*) { return nullptr; }

static const VnodeOps null_ops = {
    null_read, null_write, null_open, null_close,
    null_lseek, null_fstat, null_ioctl, null_readdir, null_lookup,
};

static Vnode null_vnode = {
    &null_ops, 2, 0, S_IFCHR, nullptr,
};

// ── console vnode ──
static int64_t console_read(Vnode*, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t console_write(Vnode*, const uint8_t* buf, uint64_t count, uint64_t) {
    service::Terminal::write(reinterpret_cast<const char*>(buf), count);
    return static_cast<int64_t>(count);
}
static int console_open(Vnode*, uint64_t) { return 0; }
static void console_close(Vnode*) {}
static int64_t console_lseek(Vnode*, int64_t, int, uint64_t*) { return -1; }
static int console_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFCHR;
    return 0;
}
static int console_ioctl(Vnode*, uint64_t, void*) { return -1; }
static int console_readdir(Vnode*, uint64_t*, Dirent*) { return -1; }
static Vnode* console_lookup(Vnode*, const char*) { return nullptr; }

static const VnodeOps console_ops = {
    console_read, console_write, console_open, console_close,
    console_lseek, console_fstat, console_ioctl, console_readdir, console_lookup,
};

static Vnode console_vnode = {
    &console_ops, 3, 0, S_IFCHR, nullptr,
};

// ── kbd vnode ──
static int64_t kbd_read(Vnode* self, uint8_t* buf, uint64_t count, uint64_t) {
    if (count == 0) return 0;
    char ch;
    for (;;) {
        if (arch::Keyboard::getchar(ch)) {
            buf[0] = static_cast<uint8_t>(ch);
            return 1;
        }
        if (self->private_data && (reinterpret_cast<uint64_t>(self->private_data) & O_NONBLOCK)) {
            return -1;
        }
        asm volatile("pause");
    }
}
static int64_t kbd_write(Vnode*, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int kbd_open(Vnode* self, uint64_t flags) {
    self->private_data = reinterpret_cast<void*>(flags);
    return 0;
}
static void kbd_close(Vnode*) {}
static int64_t kbd_lseek(Vnode*, int64_t, int, uint64_t*) { return -1; }
static int kbd_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFCHR;
    return 0;
}
static int kbd_ioctl(Vnode*, uint64_t, void*) { return -1; }
static int kbd_readdir(Vnode*, uint64_t*, Dirent*) { return -1; }
static Vnode* kbd_lookup(Vnode*, const char*) { return nullptr; }

static const VnodeOps kbd_ops = {
    kbd_read, kbd_write, kbd_open, kbd_close,
    kbd_lseek, kbd_fstat, kbd_ioctl, kbd_readdir, kbd_lookup,
};

static Vnode kbd_vnode = {
    &kbd_ops, 4, 0, S_IFCHR, nullptr,
};

// ── devfs root ──
struct DevEntry {
    const char* name;
    Vnode* vnode;
};

static DevEntry dev_entries[] = {
    {"tty", &tty_vnode},
    {"null", &null_vnode},
    {"console", &console_vnode},
    {"kbd", &kbd_vnode},
};
static const size_t dev_entry_count = sizeof(dev_entries) / sizeof(dev_entries[0]);

static int64_t dev_root_read(Vnode*, uint8_t*, uint64_t, uint64_t) { return -1; }
static int64_t dev_root_write(Vnode*, const uint8_t*, uint64_t, uint64_t) { return -1; }
static int dev_root_open(Vnode*, uint64_t) { return 0; }
static void dev_root_close(Vnode*) {}
static int64_t dev_root_lseek(Vnode*, int64_t, int, uint64_t*) { return -1; }
static int dev_root_fstat(Vnode*, VfsStat* st) {
    st->st_size = 0;
    st->st_mode = S_IFDIR;
    return 0;
}
static int dev_root_ioctl(Vnode*, uint64_t, void*) { return -1; }

static int dev_root_readdir(Vnode* self, uint64_t* pos, Dirent* dent) {
    (void)self;
    if (!pos || !dent) return -1;
    if (*pos >= dev_entry_count) return -1;
    DevEntry& e = dev_entries[*pos];
    size_t i = 0;
    while (e.name[i] && i < 63) { dent->d_name[i] = e.name[i]; ++i; }
    dent->d_name[i] = '\0';
    dent->d_ino = e.vnode->ino;
    ++(*pos);
    return 0;
}

static Vnode* dev_root_lookup(Vnode* self, const char* name) {
    (void)self;
    for (size_t i = 0; i < dev_entry_count; ++i) {
        if (strcmp(dev_entries[i].name, name) == 0) {
            return dev_entries[i].vnode;
        }
    }
    return nullptr;
}

static const VnodeOps dev_root_ops = {
    dev_root_read, dev_root_write, dev_root_open, dev_root_close,
    dev_root_lseek, dev_root_fstat, dev_root_ioctl, dev_root_readdir, dev_root_lookup,
};

static Vnode dev_root_vnode = {
    &dev_root_ops, 0, 0, S_IFDIR, nullptr,
};

static Vnode* dev_get_root() {
    return &dev_root_vnode;
}

Filesystem dev_fs = {
    "devfs",
    dev_get_root,
};

} // namespace vfs
} // namespace kernel
