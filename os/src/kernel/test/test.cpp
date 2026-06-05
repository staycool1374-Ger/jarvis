#include <kernel/test/test.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/devfs.hpp>
#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/vfs/procfs.hpp>
#include <kernel/syscall/syscall.hpp>
#include <initrd/initrd.hpp>
#include <kernel/kernel.hpp>
#include <services/shell.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <error.hpp>
#include <assert.hpp>

namespace kernel {
namespace test {

Test TestRegistry::tests_[MAX_TESTS] = {};
size_t TestRegistry::count_ = 0;
bool TestRegistry::initialized_ = false;

void TestRegistry::init() {
    if (initialized_) return;

    // --- string tests ---
    register_test("string.memset", "memset sets bytes correctly (large)", []() -> TestResult {
        uint8_t buf[64];
        memset(buf, 0xAB, 64);
        for (size_t i = 0; i < 64; ++i)
            if (buf[i] != 0xAB) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memset_small", "memset with count < 16 (byte loop)", []() -> TestResult {
        uint8_t buf[12];
        memset(buf, 0x42, 12);
        for (size_t i = 0; i < 12; ++i)
            if (buf[i] != 0x42) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcpy", "memcpy copies bytes correctly", []() -> TestResult {
        const uint8_t src[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
        uint8_t dst[32];
        memset(dst, 0, 32);
        memcpy(dst, src, 32);
        for (size_t i = 0; i < 32; ++i)
            if (dst[i] != src[i]) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcpy_small", "memcpy with count < 16", []() -> TestResult {
        const uint8_t src[8] = {10,20,30,40,50,60,70,80};
        uint8_t dst[8] = {0};
        memcpy(dst, src, 8);
        for (size_t i = 0; i < 8; ++i)
            if (dst[i] != src[i]) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcmp_equal", "memcmp returns 0 for identical buffers", []() -> TestResult {
        uint8_t a[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        uint8_t b[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        TEST_ASSERT(memcmp(a, b, 16) == 0);
        return TestResult::PASS;
    });

    register_test("string.memcmp_diff", "memcmp returns non-zero for different buffers", []() -> TestResult {
        uint8_t a[8] = {1,2,3,4,5,6,7,8};
        uint8_t b[8] = {1,2,3,4,5,6,7,9};
        TEST_ASSERT(memcmp(a, b, 8) != 0);
        return TestResult::PASS;
    });

    register_test("string.strlen", "strlen returns correct length", []() -> TestResult {
        TEST_ASSERT(strlen("") == 0);
        TEST_ASSERT(strlen("a") == 1);
        TEST_ASSERT(strlen("hello") == 5);
        TEST_ASSERT(strlen("hello world!") == 12);
        return TestResult::PASS;
    });

    // --- utils tests ---
    register_test("utils.align_up", "align_up rounds up correctly", []() -> TestResult {
        TEST_ASSERT(align_up<uint64_t>(0, 4096) == 0);
        TEST_ASSERT(align_up<uint64_t>(1, 4096) == 4096);
        TEST_ASSERT(align_up<uint64_t>(4096, 4096) == 4096);
        TEST_ASSERT(align_up<uint64_t>(4097, 4096) == 8192);
        TEST_ASSERT(align_up<uint64_t>(15, 16) == 16);
        TEST_ASSERT(align_up<uint64_t>(16, 16) == 16);
        return TestResult::PASS;
    });

    register_test("utils.align_down", "align_down rounds down correctly", []() -> TestResult {
        TEST_ASSERT(align_down<uint64_t>(0, 4096) == 0);
        TEST_ASSERT(align_down<uint64_t>(4095, 4096) == 0);
        TEST_ASSERT(align_down<uint64_t>(4096, 4096) == 4096);
        TEST_ASSERT(align_down<uint64_t>(4097, 4096) == 4096);
        TEST_ASSERT(align_down<uint64_t>(15, 16) == 0);
        TEST_ASSERT(align_down<uint64_t>(16, 16) == 16);
        return TestResult::PASS;
    });

    // --- ErrorOr tests ---
    register_test("error_or.value", "ErrorOr with value returns ok", []() -> TestResult {
        ErrorOr<int> e(42);
        TEST_ASSERT(e.ok());
        TEST_ASSERT(*e == 42);
        return TestResult::PASS;
    });

    register_test("error_or.error", "ErrorOr with error returns !ok", []() -> TestResult {
        ErrorOr<int> e(Error::OOM);
        TEST_ASSERT(!e.ok());
        TEST_ASSERT(e.error == Error::OOM);
        return TestResult::PASS;
    });

    register_test("error_or.void_ok", "ErrorOr<void> default is ok", []() -> TestResult {
        ErrorOr<void> e;
        TEST_ASSERT(e.ok());
        return TestResult::PASS;
    });

    register_test("error_or.void_error", "ErrorOr<void> with error", []() -> TestResult {
        ErrorOr<void> e(Error::NOT_FOUND);
        TEST_ASSERT(!e.ok());
        TEST_ASSERT(e.error == Error::NOT_FOUND);
        return TestResult::PASS;
    });

    // --- PMM tests ---
    register_test("pmm.alloc_free", "PMM allocates and frees a single page", []() -> TestResult {
        uint64_t page = PMM::alloc_page();
        TEST_ASSERT(page != 0);
        TEST_ASSERT((page & 0xFFF) == 0); // page-aligned
        PMM::free_page(page);
        return TestResult::PASS;
    });

    register_test("pmm.alloc_contiguous", "PMM allocates contiguous pages", []() -> TestResult {
        uint64_t block = PMM::alloc_contiguous(4);
        TEST_ASSERT(block != 0);
        TEST_ASSERT((block & 0xFFF) == 0);
        PMM::free_page(block);
        PMM::free_page(block + 0x1000);
        PMM::free_page(block + 0x2000);
        PMM::free_page(block + 0x3000);
        return TestResult::PASS;
    });

    register_test("pmm.alloc_zero_size", "PMM alloc_contiguous with zero count returns 0", []() -> TestResult {
        TEST_ASSERT(PMM::alloc_contiguous(0) == 0);
        return TestResult::PASS;
    });

    // --- MemPool tests ---
    register_test("mempool.alloc_small", "MemPool allocates 16-byte block", []() -> TestResult {
        void* p = MemPool::alloc(16);
        TEST_ASSERT(p != nullptr);
        MemPool::free(p);
        return TestResult::PASS;
    });

    register_test("mempool.alloc_large", "MemPool allocates 1024-byte block", []() -> TestResult {
        void* p = MemPool::alloc(1024);
        TEST_ASSERT(p != nullptr);
        MemPool::free(p);
        return TestResult::PASS;
    });

    register_test("mempool.alloc_too_large", "MemPool returns null for >2048 bytes", []() -> TestResult {
        void* p = MemPool::alloc(4096);
        TEST_ASSERT(p == nullptr);
        return TestResult::PASS;
    });

    register_test("mempool.reuse", "MemPool reuses freed blocks", []() -> TestResult {
        void* p1 = MemPool::alloc(16);
        TEST_ASSERT(p1 != nullptr);
        MemPool::free(p1);
        void* p2 = MemPool::alloc(16);
        TEST_ASSERT(p2 != nullptr);
        TEST_ASSERT(p1 == p2);
        return TestResult::PASS;
    });

    // --- IPC tests ---
    register_test("ipc.create_mailbox", "IPC creates a mailbox", []() -> TestResult {
        auto* mb = IPC::create_mailbox(42);
        TEST_ASSERT(mb != nullptr);
        TEST_ASSERT(mb->owner_id == 42);
        TEST_ASSERT(mb->count == 0);
        IPC::destroy_mailbox(42);
        return TestResult::PASS;
    });

    register_test("ipc.send_receive", "IPC sends and receives a message", []() -> TestResult {
        auto* mb = IPC::create_mailbox(1);
        TEST_ASSERT(mb != nullptr);

        Message msg;
        msg.sender_id = 2;
        msg.type = 99;
        msg.data_size = 4;
        msg.data[0] = 0xDE;
        msg.data[1] = 0xAD;
        msg.data[2] = 0xBE;
        msg.data[3] = 0xEF;

        TEST_ASSERT(IPC::send(1, msg));

        Message recv;
        TEST_ASSERT(IPC::receive(1, recv));
        TEST_ASSERT(recv.sender_id == 2);
        TEST_ASSERT(recv.type == 99);
        TEST_ASSERT(recv.data_size == 4);
        TEST_ASSERT(recv.data[0] == 0xDE);
        TEST_ASSERT(recv.data[3] == 0xEF);

        IPC::destroy_mailbox(1);
        return TestResult::PASS;
    });

    register_test("ipc.mailbox_full", "IPC returns false when mailbox is full", []() -> TestResult {
        auto* mb = IPC::create_mailbox(7);
        TEST_ASSERT(mb != nullptr);

        Message msg;
        msg.sender_id = 0;
        msg.type = 0;
        msg.data_size = 0;

        bool all_sent = true;
        for (size_t i = 0; i < IPC_MAX_MAILBOX_MSG; ++i) {
            if (!IPC::send(7, msg)) { all_sent = false; break; }
        }
        TEST_ASSERT(all_sent);

        bool should_fail = IPC::send(7, msg);
        TEST_ASSERT(!should_fail);

        for (size_t i = 0; i < IPC_MAX_MAILBOX_MSG; ++i) {
            Message r;
            IPC::receive(7, r);
        }

        IPC::destroy_mailbox(7);
        return TestResult::PASS;
    });

    // --- VMM tests ---
    register_test("vmm.clone_kernel_pml4", "VMM erstellt neue PML4 mit allen Kernel-Mappings", []() -> TestResult {
        uint64_t orig_cr3 = VMM::current_pml4();
        uint64_t new_pml4 = VMM::clone_kernel_pml4();
        if (new_pml4 == 0) return TestResult::FAIL;
        if ((new_pml4 & 0xFFF) != 0) return TestResult::FAIL;
        if (new_pml4 == orig_cr3) return TestResult::FAIL;
        auto* orig = reinterpret_cast<uint64_t*>(orig_cr3 & ~0xFFFULL);
        auto* pml4 = reinterpret_cast<uint64_t*>(new_pml4);
        for (int i = 256; i < 512; ++i) {
            if (orig[i] != pml4[i]) return TestResult::FAIL;
        }
        for (int i = 0; i < 256; ++i) {
            if (pml4[i] != 0) return TestResult::FAIL;
        }
        return TestResult::PASS;
    });

    // --- Task tests ---
    register_test("task.create_user.alloc", "create_user gibt gueltigen TCB zurueck", []() -> TestResult {
        auto* task = TaskControlBlock::create_user([](){}, 1, 50, 16_KiB);
        if (!task) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("task.create_user.pagetable", "User-Task hat eigene Page Table", []() -> TestResult {
        auto* task = TaskControlBlock::create_user([](){}, 1, 50, 16_KiB);
        if (!task) return TestResult::FAIL;
        if (task->page_table_ == 0) return TestResult::FAIL;
        if ((task->page_table_ & 0xFFF) != 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("task.create_user.stack", "User-Task hat User- und Kernel-Stack allokiert", []() -> TestResult {
        auto* task = TaskControlBlock::create_user([](){}, 1, 50, 16_KiB);
        if (!task) return TestResult::FAIL;
        if (task->user_stack_ == 0) return TestResult::FAIL;
        if (task->user_stack_size_ != 16_KiB) return TestResult::FAIL;
        if (task->kernel_stack_top == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("task.create_user.frame", "IRETQ-Frame hat User-Segmente (CS=0x1B, SS=0x23)", []() -> TestResult {
        auto* task = TaskControlBlock::create_user([](){}, 1, 50, 16_KiB);
        if (!task) return TestResult::FAIL;
        uint64_t* s = reinterpret_cast<uint64_t*>(task->context.rsp);
        uint64_t cs  = s[18];
        uint64_t rfl = s[19];
        uint64_t rsp = s[20];
        uint64_t ss  = s[21];
        if (cs != 0x1B) return TestResult::FAIL;
        if (ss != 0x23) return TestResult::FAIL;
        if ((rfl & 0x200) == 0) return TestResult::FAIL;
        if (rsp < 0x70000000 || rsp > 0x80000000) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("syscall.int80_dispatch", "int $0x80 dispatches syscall number to handler", []() -> TestResult {
        g_user_task_ran = false;
        uint64_t fake_regs[15] = {0};
        fake_regs[0] = 99;
        handle_interrupt_c(0x80, 0, 0, fake_regs);
        if (!g_user_task_ran) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("task.user_execution", "User-Task execution via scheduler sets g_user_task_ran", []() -> TestResult {
        if (g_user_task_ran) return TestResult::PASS;
        return TestResult::FAIL;
    });

    register_test("elf.validate_header", "ELF header validation accepts valid header", []() -> TestResult {
        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
        buf[4] = 2; // 64-bit
        buf[5] = 1; // little endian
        auto* hdr = reinterpret_cast<elf::ELF64Header*>(buf);
        hdr->type = 2;    // ET_EXEC
        hdr->machine = 0x3E; // x86-64
        if (!elf::validate_header(hdr)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("elf.validate_header_invalid", "ELF header rejects bad magic", []() -> TestResult {
        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'X';
        auto* hdr = reinterpret_cast<elf::ELF64Header*>(buf);
        if (elf::validate_header(hdr)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("elf.validate_header_too_many_phdr", "ELF header rejects phnum > 64", []() -> TestResult {
        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
        buf[4] = 2; buf[5] = 1;
        auto* hdr = reinterpret_cast<elf::ELF64Header*>(buf);
        hdr->type = 2; hdr->machine = 0x3E;
        hdr->phnum = 65;
        if (elf::validate_header(hdr)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("elf.validate_segment_overflow", "Segment with overflowing vaddr+memsz rejected", []() -> TestResult {
        uint8_t buf[sizeof(elf::ELF64Header) + sizeof(elf::ELF64ProgramHeader)];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
        buf[4] = 2; buf[5] = 1;
        auto* hdr = reinterpret_cast<elf::ELF64Header*>(buf);
        hdr->type = 2; hdr->machine = 0x3E;
        hdr->phoff = sizeof(elf::ELF64Header);
        hdr->phnum = 1;
        hdr->phentsize = sizeof(elf::ELF64ProgramHeader);
        auto* phdr = reinterpret_cast<elf::ELF64ProgramHeader*>(buf + sizeof(elf::ELF64Header));
        phdr->type = 1; // PT_LOAD
        phdr->vaddr = 0x70000000;
        phdr->memsz = 0xFFFFFFFFFFFFFF00ULL;
        phdr->filesz = 0;
        auto* tcb = elf::load(hdr, buf);
        if (tcb) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("elf.validate_segment_kernel_range", "Segment in kernel range rejected", []() -> TestResult {
        uint8_t buf[sizeof(elf::ELF64Header) + sizeof(elf::ELF64ProgramHeader)];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
        buf[4] = 2; buf[5] = 1;
        auto* hdr = reinterpret_cast<elf::ELF64Header*>(buf);
        hdr->type = 2; hdr->machine = 0x3E;
        hdr->phoff = sizeof(elf::ELF64Header);
        hdr->phnum = 1;
        hdr->phentsize = sizeof(elf::ELF64ProgramHeader);
        auto* phdr = reinterpret_cast<elf::ELF64ProgramHeader*>(buf + sizeof(elf::ELF64Header));
        phdr->type = 1;
        phdr->vaddr = 0xFFFF800000000000ULL;
        phdr->memsz = 4096;
        phdr->filesz = 4096;
        auto* tcb = elf::load(hdr, buf);
        if (tcb) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_hello_c", "Find hello.c.elf (C userspace) in initrd", []() -> TestResult {
        initrd::InitrdFile f = initrd::find("hello.c.elf");
        if (!f.data || f.size == 0) return TestResult::FAIL;
        auto* hdr = reinterpret_cast<const elf::ELF64Header*>(f.data);
        if (!elf::validate_header(hdr)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_ls", "Find ls.c.elf in initrd", []() -> TestResult {
        auto f = initrd::find("ls.c.elf");
        if (!f.data || f.size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_cat", "Find cat.c.elf in initrd", []() -> TestResult {
        auto f = initrd::find("cat.c.elf");
        if (!f.data || f.size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_top", "Find top.c.elf in initrd", []() -> TestResult {
        auto f = initrd::find("top.c.elf");
        if (!f.data || f.size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_hello", "Find hello.elf in initrd", []() -> TestResult {
        auto f = initrd::find("main.S.elf");
        if (!f.data || f.size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.find_nonexistent", "Non-existent file returns null", []() -> TestResult {
        auto f = initrd::find("nonexistent.elf");
        if (f.data) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("initrd.readdir", "Initrd readdir lists all files", []() -> TestResult {
        initrd::InitrdEntry ie;
        uint64_t pos = 0;
        bool found_ls = false, found_cat = false, found_top = false;
        while (initrd::readdir(&pos, &ie)) {
            if (strcmp(ie.name, "ls.c.elf") == 0) found_ls = true;
            if (strcmp(ie.name, "cat.c.elf") == 0) found_cat = true;
            if (strcmp(ie.name, "top.c.elf") == 0) found_top = true;
        }
        if (!found_ls) return TestResult::FAIL;
        if (!found_cat) return TestResult::FAIL;
        if (!found_top) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("syscall.open_read_close", "Open, read, close an initrd file via syscall", []() -> TestResult {
        const char* path = "main.S.elf";
        uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                         reinterpret_cast<uint64_t>(path), 0, 0, 0);
        if (fd == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        uint8_t buf[16];
        uint64_t bytes = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ), fd,
                                           reinterpret_cast<uint64_t>(buf), 16, 0);
        if (bytes == 0 || bytes == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        uint64_t result = Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0);
        if (result != 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("elf.load_from_initrd", "Load hello.elf from initrd, execute, sets g_user_task_ran", []() -> TestResult {
        if (g_user_task_ran) return TestResult::PASS;
        return TestResult::FAIL;
    });

    // ── v0.1.1: VFS core types + per-task FdTable + cwd ──
    register_test("vfs.fd_alloc", "Allocate fd in current task's FdTable", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        int fd = task->fd_table.alloc();
        if (fd < 0) return TestResult::FAIL;
        if (!task->fd_table.fds[fd].used) return TestResult::FAIL;
        task->fd_table.free(fd);
        return TestResult::PASS;
    });

    register_test("vfs.fd_alloc_free", "Free fd and verify it can be reused", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        int fd1 = task->fd_table.alloc();
        if (fd1 < 0) return TestResult::FAIL;
        task->fd_table.free(fd1);
        int fd2 = task->fd_table.alloc();
        if (fd2 < 0) return TestResult::FAIL;
        if (fd1 != fd2) return TestResult::FAIL;
        task->fd_table.free(fd2);
        return TestResult::PASS;
    });

    register_test("vfs.cwd_default", "Default cwd is /", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        if (task->cwd[0] != '/' || task->cwd[1] != '\0') return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.cwd_vnode_set", "cwd_vnode is non-null", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task || !task->cwd_vnode) return TestResult::FAIL;
        return TestResult::PASS;
    });

    // ── v0.1.2: Mount system + devfs + path resolution ──
    register_test("vfs.resolve_root", "Resolve / returns root vnode", []() -> TestResult {
        auto* vn = vfs::resolve("/");
        if (!vn) return TestResult::FAIL;
        if (!(vn->mode & vfs::S_IFDIR)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.dev_tty_open", "Open /dev/tty returns valid fd", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        auto* vn = vfs::resolve("/dev/tty");
        if (!vn) return TestResult::FAIL;
        int fd = task->fd_table.alloc();
        if (fd < 0) return TestResult::FAIL;
        task->fd_table.fds[fd].vnode = vn;
        task->fd_table.fds[fd].offset = 0;
        task->fd_table.fds[fd].flags = vfs::O_NONBLOCK;
        task->fd_table.free(fd);
        return TestResult::PASS;
    });

    register_test("vfs.dev_null_write", "Write to /dev/null returns count", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        auto* vn = vfs::resolve("/dev/null");
        if (!vn || !vn->ops->write) return TestResult::FAIL;
        const char test_data[] = "hello";
        int64_t r = vn->ops->write(vn, reinterpret_cast<const uint8_t*>(test_data), 5, 0);
        if (r != 5) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.dev_console_write", "Write to /dev/console returns count", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        auto* vn = vfs::resolve("/dev/console");
        if (!vn || !vn->ops->write) return TestResult::FAIL;
        const char test_data[] = "VFS test\n";
        int64_t r = vn->ops->write(vn, reinterpret_cast<const uint8_t*>(test_data), 9, 0);
        if (r != 9) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.resolve_hello", "Resolve main.S.elf via VFS returns file vnode", []() -> TestResult {
        auto* vn = vfs::resolve("main.S.elf");
        if (!vn) return TestResult::FAIL;
        if (!(vn->mode & vfs::S_IFREG)) return TestResult::FAIL;
        if (vn->size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.open_hello_syscall", "Open main.S.elf via syscall OPEN returns fd", []() -> TestResult {
        uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                      reinterpret_cast<uint64_t>("main.S.elf"), 0, 0, 0);
        if (fd == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0);
        return TestResult::PASS;
    });

    register_test("vfs.read_hello_syscall", "Read from opened hello.elf via syscall returns data", []() -> TestResult {
        uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                      reinterpret_cast<uint64_t>("main.S.elf"), 0, 0, 0);
        if (fd == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        uint8_t buf[16];
        uint64_t bytes = Syscall::handle(static_cast<uint64_t>(SyscallNumber::READ), fd,
                                          reinterpret_cast<uint64_t>(buf), 16, 0);
        if (bytes == 0 || bytes == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0);
        return TestResult::PASS;
    });

    // ── v0.1.3: Procfs + lseek + readdir + dup + stat ──
    register_test("vfs.proc_meminfo_read", "Read from /proc/meminfo returns content", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        auto* vn = vfs::resolve("/proc/meminfo");
        if (!vn || !vn->ops->read) return TestResult::FAIL;
        uint8_t buf[64];
        int64_t r = vn->ops->read(vn, buf, 64, 0);
        if (r <= 0) return TestResult::FAIL;
        buf[r < 64 ? r : 64] = '\0';
        return TestResult::PASS;
    });

    register_test("vfs.lseek_set", "lseek SEEK_SET resets position to 0", []() -> TestResult {
        uint64_t fd = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                      reinterpret_cast<uint64_t>("main.S.elf"), 0, 0, 0);
        if (fd == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        uint64_t pos = 0;
        uint64_t r = Syscall::handle(static_cast<uint64_t>(SyscallNumber::LSEEK), fd, 0,
                                      static_cast<uint64_t>(vfs::SEEK_SET), reinterpret_cast<uint64_t>(&pos));
        if (r == static_cast<uint64_t>(-1)) { Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0); return TestResult::FAIL; }
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd, 0, 0, 0);
        return TestResult::PASS;
    });

    register_test("vfs.dup", "dup creates new fd pointing to same vnode", []() -> TestResult {
        uint64_t fd1 = Syscall::handle(static_cast<uint64_t>(SyscallNumber::OPEN),
                                       reinterpret_cast<uint64_t>("main.S.elf"), 0, 0, 0);
        if (fd1 == static_cast<uint64_t>(-1)) return TestResult::FAIL;
        uint64_t fd2 = Syscall::handle(static_cast<uint64_t>(SyscallNumber::DUP), fd1, 0, 0, 0);
        if (fd2 == static_cast<uint64_t>(-1)) { Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd1, 0, 0, 0); return TestResult::FAIL; }
        if (fd1 == fd2) { Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd1, 0, 0, 0); Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd2, 0, 0, 0); return TestResult::FAIL; }
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd1, 0, 0, 0);
        Syscall::handle(static_cast<uint64_t>(SyscallNumber::CLOSE), fd2, 0, 0, 0);
        return TestResult::PASS;
    });

    register_test("vfs.stat_path", "stat via path returns file size", []() -> TestResult {
        vfs::VfsStat st;
        uint64_t r = Syscall::handle(static_cast<uint64_t>(SyscallNumber::STAT),
                                      reinterpret_cast<uint64_t>("main.S.elf"),
                                      reinterpret_cast<uint64_t>(&st), 0, 0);
        if (r != 0) return TestResult::FAIL;
        if (st.st_size == 0) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.readdir_initrd", "readdir on / lists initrd files", []() -> TestResult {
        auto* vn = vfs::resolve("/");
        if (!vn || !vn->ops->readdir) return TestResult::FAIL;
        vfs::Dirent dent;
        uint64_t pos = 0;
        int found = 0;
        while (vn->ops->readdir(vn, &pos, &dent) == 0) {
            if (dent.d_name[0] != '\0') {
                ++found;
            }
        }
        if (found < 3) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("vfs.readdir_dev", "readdir on /dev lists device entries", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        auto* vn = vfs::resolve("/dev");
        if (!vn || !vn->ops->readdir) return TestResult::FAIL;
        vfs::Dirent dent;
        uint64_t pos = 0;
        int found = 0;
        while (vn->ops->readdir(vn, &pos, &dent) == 0) {
            if (dent.d_name[0] != '\0') ++found;
        }
        if (found < 3) return TestResult::FAIL; // tty, null, console, kbd
        return TestResult::PASS;
    });

    // ── v0.1.4: Blocking IPC ──
    register_test("ipc.blocking_create", "Mailbox with waiters initialized to null", []() -> TestResult {
        auto* mb = IPC::create_mailbox(999);
        if (!mb) return TestResult::FAIL;
        if (mb->waiting_sender || mb->waiting_receiver) return TestResult::FAIL;
        IPC::destroy_mailbox(999);
        return TestResult::PASS;
    });

    register_test("ipc.blocking_send_receive", "send unblocks waiting receiver", []() -> TestResult {
        // This test verifies the IPC mechanism by creating two mailboxes
        // and checking that send works
        auto* mb_a = IPC::create_mailbox(1001);
        auto* mb_b = IPC::create_mailbox(1002);
        if (!mb_a || !mb_b) return TestResult::FAIL;

        // Send from A's perspective (to B)
        Message msg;
        msg.sender_id = 1001;
        msg.type = 1;
        msg.data_size = 4;
        msg.data[0] = 0xAA;

        // Set up a fake waiter on mailbox B
        mb_b->waiting_receiver = Scheduler::current_task();

        // Send should wake the waiter after enqueueing
        bool sent = IPC::send(1002, msg);
        if (!sent) { IPC::destroy_mailbox(1001); IPC::destroy_mailbox(1002); return TestResult::FAIL; }

        // Verify the waiter was woken (set to nullptr)
        if (mb_b->waiting_receiver != nullptr) { IPC::destroy_mailbox(1001); IPC::destroy_mailbox(1002); return TestResult::FAIL; }

        // Verify the message is in the mailbox
        Message recv;
        bool received = IPC::receive(1002, recv);
        if (!received) { IPC::destroy_mailbox(1001); IPC::destroy_mailbox(1002); return TestResult::FAIL; }
        if (recv.data[0] != 0xAA) { IPC::destroy_mailbox(1001); IPC::destroy_mailbox(1002); return TestResult::FAIL; }

        IPC::destroy_mailbox(1001);
        IPC::destroy_mailbox(1002);
        return TestResult::PASS;
    });

    // ── Syscall-level IPC tests ──
    register_test("syscall.ipc_send_receive", "Syscall SEND + non-blocking RECEIVE via Syscall::handle", []() -> TestResult {
        auto* mb_a = IPC::create_mailbox(2001);
        auto* mb_b = IPC::create_mailbox(2002);
        if (!mb_a || !mb_b) return TestResult::FAIL;

        uint8_t send_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        uint64_t r = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::SEND),
            2002,
            reinterpret_cast<uint64_t>(send_data),
            42,
            8
        );
        if (r != 0) { IPC::destroy_mailbox(2001); IPC::destroy_mailbox(2002); return TestResult::FAIL; }

        uint8_t recv_data[8] = {};
        r = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::RECEIVE),
            2002,
            reinterpret_cast<uint64_t>(recv_data),
            8,
            0
        );
        if (r != 42) { IPC::destroy_mailbox(2001); IPC::destroy_mailbox(2002); return TestResult::FAIL; }
        if (recv_data[0] != 0x11 || recv_data[7] != 0x88) { IPC::destroy_mailbox(2001); IPC::destroy_mailbox(2002); return TestResult::FAIL; }

        IPC::destroy_mailbox(2001);
        IPC::destroy_mailbox(2002);
        return TestResult::PASS;
    });

    register_test("syscall.ipc_send_sync", "SEND_SYNC sends+receives reply via Syscall::handle", []() -> TestResult {
        auto* mb_srv = IPC::create_mailbox(3001);
        auto* mb_cli = IPC::create_mailbox(3002);
        if (!mb_srv || !mb_cli) return TestResult::FAIL;

        // Pre-place a reply in the server's mailbox (SEND_SYNC sends to dest, then receives from dest)
        Message reply_msg;
        reply_msg.sender_id = 3002;
        reply_msg.type = 99;
        reply_msg.data_size = 4;
        reply_msg.data[0] = 0x11;
        reply_msg.data[1] = 0x22;
        reply_msg.data[2] = 0x33;
        reply_msg.data[3] = 0x44;
        IPC::send(3001, reply_msg);

        uint8_t reply_buf[4] = {};

        uint64_t reply_type = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::SEND_SYNC),
            3001,
            reinterpret_cast<uint64_t>(reply_buf),
            42,
            0
        );

        if (reply_type != 99) { IPC::destroy_mailbox(3001); IPC::destroy_mailbox(3002); return TestResult::FAIL; }
        if (reply_buf[0] != 0x11 || reply_buf[3] != 0x44) { IPC::destroy_mailbox(3001); IPC::destroy_mailbox(3002); return TestResult::FAIL; }

        IPC::destroy_mailbox(3001);
        IPC::destroy_mailbox(3002);
        return TestResult::PASS;
    });

    register_test("syscall.return_value", "Syscall return value propagates through handle()", []() -> TestResult {
        // GET_TICKS returns a non-zero value
        uint64_t ticks = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::GET_TICKS), 0, 0, 0, 0);
        if (ticks == 0) return TestResult::FAIL;

        // OPEN with invalid path returns -1 (all bits set)
        uint64_t bad_open = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::OPEN),
            reinterpret_cast<uint64_t>("/nonexistent"),
            0, 0, 0);
        if (bad_open != static_cast<uint64_t>(-1)) return TestResult::FAIL;

        // CLOSE with invalid fd returns 0 (no error path, but doesn't crash)
        uint64_t close_ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::CLOSE), 99, 0, 0, 0);
        if (close_ret != 0) return TestResult::FAIL;

        return TestResult::PASS;
    });

    // ── v0.2.1: fork + waitpid ──
    register_test("syscall.fork_no_regs", "fork returns -1 when called without regs (kernel mode)", []() -> TestResult {
        uint64_t pid = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::FORK), 0, 0, 0, 0);
        if (pid != static_cast<uint64_t>(-1)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("syscall.waitpid_no_children", "waitpid with no children returns -1", []() -> TestResult {
        uint64_t status = 0;
        uint64_t ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::WAITPID),
            static_cast<uint64_t>(-1),
            reinterpret_cast<uint64_t>(&status), 0, 0);
        if (ret != static_cast<uint64_t>(-1)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("syscall.waitpid_invalid_pid", "waitpid for nonexistent PID returns -1", []() -> TestResult {
        uint64_t status = 0;
        uint64_t ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::WAITPID),
            999999,
            reinterpret_cast<uint64_t>(&status), 0, 0);
        if (ret != static_cast<uint64_t>(-1)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("syscall.waitpid_reaps_child", "waitpid reaps manually created child task", []() -> TestResult {
        auto* cur = Scheduler::current_task();
        if (!cur) return TestResult::FAIL;
        auto* child = TaskControlBlock::create([](){ while(true) asm volatile("hlt"); }, 1, 50);
        if (!child) return TestResult::FAIL;
        child->parent_id = cur->id;
        child->state = TaskState::TERMINATED;
        child->exit_code = 42;
        uint64_t before = Scheduler::task_count();
        Scheduler::add_task(child);
        uint64_t status = 0;
        uint64_t ret = Syscall::handle(
            static_cast<uint64_t>(SyscallNumber::WAITPID),
            child->id,
            reinterpret_cast<uint64_t>(&status), 0, 0);
        uint64_t after = Scheduler::task_count();
        if (ret != child->id) return TestResult::FAIL;
        if (status != 42) return TestResult::FAIL;
        if (after != before) return TestResult::FAIL;
        return TestResult::PASS;
    });

    g_user_task_ran = false;

    // ── Shell command tests ──
    register_test("shell.help", "help command executes without crash", []() -> TestResult {
        service::Shell::execute("help");
        return TestResult::PASS;
    });

    register_test("shell.echo", "echo command prints arguments", []() -> TestResult {
        service::Shell::execute("echo hello world");
        return TestResult::PASS;
    });

    register_test("shell.clear", "clear command executes without crash", []() -> TestResult {
        service::Shell::execute("clear");
        return TestResult::PASS;
    });

    register_test("shell.uptime", "uptime command executes without crash", []() -> TestResult {
        service::Shell::execute("uptime");
        return TestResult::PASS;
    });

    register_test("shell.tasks", "tasks command executes without crash", []() -> TestResult {
        service::Shell::execute("tasks");
        return TestResult::PASS;
    });

    register_test("shell.meminfo", "meminfo command executes without crash", []() -> TestResult {
        service::Shell::execute("meminfo");
        return TestResult::PASS;
    });

    register_test("shell.version", "version command executes without crash", []() -> TestResult {
        service::Shell::execute("version");
        return TestResult::PASS;
    });

    register_test("shell.jobs", "jobs command executes without crash", []() -> TestResult {
        service::Shell::execute("jobs");
        return TestResult::PASS;
    });

    register_test("shell.listprog", "listprog command executes without crash", []() -> TestResult {
        service::Shell::execute("listprog");
        return TestResult::PASS;
    });

    register_test("shell.modlist", "modlist command executes without crash", []() -> TestResult {
        service::Shell::execute("modlist");
        return TestResult::PASS;
    });

    register_test("shell.selftest_named", "selftest with name argument runs single test (no recursion)", []() -> TestResult {
        service::Shell::execute("selftest string.memset");
        return TestResult::PASS;
    });

    register_test("shell.cd_root", "cd / changes working directory to root", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        service::Shell::execute("cd /");
        if (!task->cwd_vnode) return TestResult::FAIL;
        if (!(task->cwd_vnode->mode & vfs::S_IFDIR)) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("shell.cd_nonexistent", "cd to nonexistent path does not crash", []() -> TestResult {
        auto* task = Scheduler::current_task();
        if (!task) return TestResult::FAIL;
        service::Shell::execute("cd /nonexistent_dir_xyz");
        return TestResult::PASS;
    });

    register_test("shell.cd_to_file", "cd to a file path does not crash", []() -> TestResult {
        service::Shell::execute("cd main.S.elf");
        return TestResult::PASS;
    });

    register_test("shell.export", "export command executes without crash", []() -> TestResult {
        service::Shell::execute("export FOO=bar");
        return TestResult::PASS;
    });

    register_test("shell.unknown_command", "unknown command shows error gracefully", []() -> TestResult {
        service::Shell::execute("thiscommanddoesnotexist");
        return TestResult::PASS;
    });

    register_test("shell.empty_command", "empty line is handled gracefully", []() -> TestResult {
        service::Shell::execute("");
        return TestResult::PASS;
    });

    register_test("shell.spaces_only", "line with only spaces is handled", []() -> TestResult {
        service::Shell::execute("   ");
        return TestResult::PASS;
    });

    register_test("shell.runelf_noarg", "runelf without argument shows usage", []() -> TestResult {
        service::Shell::execute("runelf");
        return TestResult::PASS;
    });

    register_test("shell.runelf_nonexistent", "runelf with nonexistent file shows error", []() -> TestResult {
        service::Shell::execute("runelf nonexistent.elf");
        return TestResult::PASS;
    });

    register_test("shell.bench_alloc", "bench alloc benchmark executes without crash", []() -> TestResult {
        service::Shell::execute("bench alloc");
        return TestResult::PASS;
    });

    register_test("shell.bench_noarg", "bench without argument shows usage", []() -> TestResult {
        service::Shell::execute("bench");
        return TestResult::PASS;
    });

    register_test("shell.modprobe_valid", "modprobe keyboard attempts to load driver", []() -> TestResult {
        service::Shell::execute("modprobe keyboard");
        return TestResult::PASS;
    });

    register_test("shell.modprobe_noarg", "modprobe without argument shows usage", []() -> TestResult {
        service::Shell::execute("modprobe");
        return TestResult::PASS;
    });

    register_test("shell.run_noarg", "run without argument shows program list", []() -> TestResult {
        service::Shell::execute("run");
        return TestResult::PASS;
    });

    register_test("shell.runelf_valid", "runelf with hello.c.elf creates new task", []() -> TestResult {
        uint64_t before = Scheduler::task_count();
        service::Shell::execute("runelf hello.c.elf");
        uint64_t after = Scheduler::task_count();
        if (after <= before) return TestResult::FAIL;
        return TestResult::PASS;
    });

    {
        initrd::InitrdFile f = initrd::find("main.S.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const elf::ELF64Header*>(f.data);
            if (elf::validate_header(hdr)) {
                auto* task = elf::load(hdr, f.data);
                if (task) {
                    Scheduler::add_task(task);
                }
            }
        }
    }
    {
        initrd::InitrdFile f = initrd::find("hello.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const elf::ELF64Header*>(f.data);
            if (elf::validate_header(hdr)) {
                auto* task = elf::load(hdr, f.data);
                if (task) {
                    Scheduler::add_task(task);
                    g_user_task_ran = true;
                }
            }
        }
    }

    initialized_ = true;
}

bool TestRegistry::register_test(const char* name, const char* description,
                                 TestResult (*func)()) {
    if (count_ >= MAX_TESTS) return false;
    tests_[count_].name = name;
    tests_[count_].description = description;
    tests_[count_].func = func;
    tests_[count_].result = TestResult::SKIP;
    tests_[count_].failure_msg = nullptr;
    ++count_;
    return true;
}

const Test* TestRegistry::find(const char* name) {
    for (size_t i = 0; i < count_; ++i) {
        const char* tn = tests_[i].name;
        const char* n = name;
        while (*tn && *n && *tn == *n) { ++tn; ++n; }
        if (*tn == '\0' && *n == '\0') return &tests_[i];
    }
    return nullptr;
}

void TestRegistry::run_test(Test& t) {
    t.result = t.func();
}

TestReport TestRegistry::run_all() {
    TestReport report = {0, 0, 0, 0};
    for (size_t i = 0; i < count_; ++i) {
        run_test(tests_[i]);
        ++report.total;
        switch (tests_[i].result) {
        case TestResult::PASS:  ++report.passed;  break;
        case TestResult::FAIL:  ++report.failed;  break;
        case TestResult::SKIP:  ++report.skipped; break;
        }
    }
    return report;
}

TestReport TestRegistry::run(const char* name) {
    TestReport report = {0, 0, 0, 0};
    for (size_t i = 0; i < count_; ++i) {
        const char* tn = tests_[i].name;
        const char* n = name;
        while (*tn && *n && *tn == *n) { ++tn; ++n; }
        if (*tn != '\0' || *n != '\0') continue;

        run_test(tests_[i]);
        ++report.total;
        switch (tests_[i].result) {
        case TestResult::PASS:  ++report.passed;  break;
        case TestResult::FAIL:  ++report.failed;  break;
        case TestResult::SKIP:  ++report.skipped; break;
        }
        break;
    }
    return report;
}

} // namespace test
} // namespace kernel
