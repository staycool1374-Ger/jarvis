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

#include <kernel/kernel.hpp>
#include <kernel/arch/gdt.hpp>
#include <kernel/arch/idt.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/rtc.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/keyboard.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/ipc/buffer_pool.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/driver/driver.hpp>
#include <version.hpp>
#include <kernel/bootparams.hpp>
#include <kernel/sync/sync.hpp>
#include <kernel/multiboot2.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/vfs/vfsd.hpp>
#include <kernel/driver/iocd.hpp>
#include <kernel/driver/ata_pio.hpp>
#include <kernel/driver/ahci.hpp>
#include <kernel/driver/virtio_net.hpp>
#include <kernel/net/net.hpp>
#include <kernel/vfs/fat32_fs.hpp>
#include <kernel/daemon/daemon_mgr.hpp>
#include <kernel/vfs/initrd_fs.hpp>
#include <kernel/random.hpp>
#include <kernel/vfs/devfs.hpp>
#include <kernel/vfs/procfs.hpp>
#include <kernel/vfs/tmpfs.hpp>
#include <initrd/initrd.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/terminal.hpp>
#include <services/program.hpp>
#include <services/shell.hpp>
#include <programs/demo/demo.hpp>
#include <logger.hpp>
#include <test.hpp>
#include <kernel/test/test_config.hpp>
#include <constants.hpp>
#include <signal.hpp>

#ifdef CONFIG_PROFILING
extern "C" void gcov_flush_to_serial();
#endif

using namespace arch;

static void debug_putchar(char c) {
    if (c == '\n') {
        while ((inb(arch::COM1_LSR) & 0x20) == 0);
        outb(arch::COM1, '\r');
    }
    while ((inb(arch::COM1_LSR) & 0x20) == 0);
    outb(arch::COM1, c);
}

extern "C" void debug_write(const char* s) {
    while (*s) debug_putchar(*s++);
}

extern "C" {
uint64_t* scheduler_save_rsp_to = nullptr;
uint64_t scheduler_load_rsp_from = 0;
uint64_t scheduler_load_cr3_from = 0;
uint64_t scheduler_next_task_id = 0;
uint64_t isr_nesting_depth = 0;
uint64_t scheduler_corruption_count = 0;
kernel::TaskControlBlock* fpu_owner = nullptr;
}

extern "C" void debug_write_hex(uint64_t value) {
    char hb[17] = "0000000000000000";
    int pos = 16;
    do {
        hb[--pos] = "0123456789ABCDEF"[value & 0xF];
        value >>= 4;
    } while (value);
    debug_write(hb + pos);
}

extern "C" void debug_task_switch(uint64_t old_id, uint64_t new_id, uint64_t cr3
    ) {
    debug_write("[SWITCH] old="); debug_write_hex(old_id);
    debug_write(" new="); debug_write_hex(new_id);
    debug_write(" cr3="); debug_write_hex(cr3);
    debug_write("\n");
}

extern "C" {
    uint64_t multiboot_magic = 0;
    uint64_t multiboot_info_ptr = 0;
}
extern char kernel_virt_end[];
extern "C" uint8_t _binary_initrd_cpio_start[];
extern "C" uint8_t _binary_initrd_cpio_end[];

static void init_pic() {
    outb(arch::PIC1_CMD, 0x11);
    outb(arch::PIC2_CMD, 0x11);
    outb(arch::PIC1_DATA, 0x20);
    outb(arch::PIC2_DATA, 0x28);
    outb(arch::PIC1_DATA, 0x04);
    outb(arch::PIC2_DATA, 0x02);
    outb(arch::PIC1_DATA, 0x01);
    outb(arch::PIC2_DATA, 0x01);
    outb(arch::PIC1_DATA, 0x00);
    outb(arch::PIC2_DATA, 0x00);
}

extern "C" void higherhalf_entry(uint64_t magic, uint64_t mb_info) {
    multiboot_magic = magic;
    multiboot_info_ptr = mb_info;
    extern const uint64_t kernel_stack_top;
    asm volatile("mov %0, %%rsp\n" : : "r"(kernel_stack_top));

    kernel::Logger::init();
    kernel::test::Registry::init();
    debug_write("[BOOT] ");
    debug_write(kernel::Version::string());

#ifdef CONFIG_DEBUG
    debug_write(" [DEBUG]");
#else
    debug_write(" [RELEASE]");
#endif
    debug_write("\n");

    debug_write("[BOOT] Arch init...\n");
    arch::GDT::init();
    arch::GDT::load();
    arch::GDT::set_tss_rsp0(kernel_stack_top);
    arch::IDT::init();
    arch::IDT::load();

    // Enable x87 FPU: clear CR0.EM (bit 2), set CR0.NE (bit 5), set CR0.MP (bit 1)
    uint64_t cr0 = arch::read_cr0();
    cr0 &= ~(1ULL << 2);   // clear EM
    cr0 |= (1ULL << 1);    // set MP
    cr0 |= (1ULL << 5);    // set NE
    arch::write_cr0(cr0);

    // Enable FXSAVE/FXRSTOR and SSE: set CR4.OSFXSR (bit 9) and CR4.OSXMMEXCPT (bit 10)
    uint64_t cr4 = arch::read_cr4();
    cr4 |= (1ULL << 9);    // OSFXSR
    cr4 |= (1ULL << 10);   // OSXMMEXCPT
    arch::write_cr4(cr4);

    uint64_t mem_size = 64_MiB;
    uint64_t tag_addr = mb2_find_tag(6);
    if (tag_addr) {
        auto* mem_tag = reinterpret_cast<MemoryMapTag*>(tag_addr);
        uint64_t entries = (mem_tag->size - sizeof(MemoryMapTag)
            ) / mem_tag->entry_size;
        for (uint64_t i = 0; i < entries; ++i) {
            auto& entry = mem_tag->entries[i];
            if (entry.type == 1) {
                uint64_t end = entry.base_addr + entry.length;
                if (end > mem_size) mem_size = end;
            }
        }
    }

    uint64_t kend = reinterpret_cast<uint64_t>(kernel_virt_end) - arch::
        HHDM_OFFSET;
    kernel::PMM::init(mem_size, arch::PAGE_SIZE_2M, kend);
    kernel::VMM::init();
    kernel::BootParams::parse_multiboot_cmdline();
    {
        auto& bp = kernel::BootParams::instance();
        debug_write("[BOOT] timer_hz=");
        debug_write_hex(bp.timer_hz);
        debug_write(" preempt=");
        debug_write(bp.preempt_enabled ? "yes" : "no");
        debug_write(" max_tasks=");
        debug_write_hex(bp.max_tasks);
        debug_write("\n");
    }
    debug_write("[BOOT] Memory init done\n");

    debug_write("[BOOT] Kernel init...\n");
    kernel::MemPool::init();
    initrd::init(_binary_initrd_cpio_start, _binary_initrd_cpio_end);
    kernel::vfs::init();
    kernel::vfs::mount(kernel::vfs::initrd_fs, "/");
    kernel::IPC::init();
    kernel::Syscall::init();
    kernel::Scheduler::init();

    // Launch init (PID 1) — mounts fstab, runs /etc/rc, then reaps
    {
        auto* init_task = kernel::TaskControlBlock::create(
            []() {
                // Read /etc/fstab and mount entries
                {
                    auto fstab = initrd::find("./etc/fstab");
                    if (fstab.data) {
                        const char* p = reinterpret_cast<const char*>(fstab.data);
                        const char* end = p + fstab.size;
                        while (p < end) {
                            while (p < end && (*p == ' ' || *p == '\t')) ++p;
                            if (p >= end || *p == '#' || *p == '\n') {
                                while (p < end && *p != '\n') ++p;
                                if (p < end) ++p;
                                continue;
                            }
                            char fs_name[32];
                            char mp[64];
                            int n = 0;
                            while (p < end && *p != ' ' && *p != '\t' && *p != '\n'
                                   && n < 31) fs_name[n++] = *p++;
                            fs_name[n] = '\0';
                            while (p < end && (*p == ' ' || *p == '\t')) ++p;
                            n = 0;
                            while (p < end && *p != ' ' && *p != '\t' && *p != '\n'
                                   && n < 63) mp[n++] = *p++;
                            mp[n] = '\0';
                            while (p < end && *p != '\n') ++p;
                            if (p < end) ++p;
                            if (fs_name[0] && mp[0]) {
                                auto* fs = kernel::vfs::find_fs(fs_name);
                                if (fs && kernel::vfs::mount(*fs, mp) == 0) {
                                    kernel::Logger::info("init: mounted %s at %s",
                                                 fs_name, mp);
                                }
                            }
                        }
                    }
                }
                // Read /etc/rc and execute each command
                {
                    auto rc = initrd::find("./etc/rc");
                    if (rc.data) {
                        const char* p = reinterpret_cast<const char*>(rc.data);
                        const char* end = p + rc.size;
                        while (p < end) {
                            while (p < end && (*p == ' ' || *p == '\t')) ++p;
                            if (p >= end || *p == '#' || *p == '\n') {
                                while (p < end && *p != '\n') ++p;
                                if (p < end) ++p;
                                continue;
                            }
                            char line[128];
                            int n = 0;
                            while (p < end && *p != '\n' && n < 127) line[n++] = *p++;
                            line[n] = '\0';
                            if (p < end) ++p;
                            if (line[0] == '\0') continue;
                            // Try to run "line.elf" from initrd
                            char elf_path[160];
                            n = 0;
                            const char* src = line;
                            while (*src && *src != ' ' && *src != '\t' && n < 127)
                                elf_path[n++] = *src++;
                            elf_path[n] = '\0';
                            // Also try adding .elf and .c.elf
                            char elf_path1[160], elf_path2[160];
                            int m = 0;
                            for (int i = 0; elf_path[i]; ++i)
                                elf_path1[m++] = elf_path[i];
                            elf_path1[m++] = '.'; elf_path1[m++] = 'e';
                            elf_path1[m++] = 'l'; elf_path1[m++] = 'f';
                            elf_path1[m] = '\0';
                            m = 0;
                            for (int i = 0; elf_path[i]; ++i)
                                elf_path2[m++] = elf_path[i];
                            elf_path2[m++] = '.'; elf_path2[m++] = 'c';
                            elf_path2[m++] = '.'; elf_path2[m++] = 'e';
                            elf_path2[m++] = 'l'; elf_path2[m++] = 'f';
                            elf_path2[m] = '\0';
                            auto f = initrd::find(elf_path);
                            if (!f.data) f = initrd::find(elf_path1);
                            if (!f.data) f = initrd::find(elf_path2);
                            if (f.data) {
                                auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
                                if (kernel::elf::validate_header(hdr)) {
                                    auto* task = kernel::elf::load(hdr, f.data);
                                    if (task) {
                                        task->priority = 1;
                                        task->period_ticks = 20;
                                        kernel::Scheduler::add_task(*task);
                                        kernel::Logger::info("init: started %s", elf_path);
                                    }
                                }
                            }
                        }
                    }
                }
                // Reap loop (PID 1)
                for (;;) {
                    kernel::Scheduler::reap_orphans();
                    arch::hlt();
                }
            },
            0,    // highest priority
            10);  // period_ticks
        if (init_task) {
            kernel::Scheduler::add_task(*init_task);
            kernel::Logger::info("init: PID 1 started");
        }
    }

    kernel::PMM::set_oom_handler([]() -> bool {
        kernel::TaskControlBlock* victim = nullptr;
        uint64_t victim_priority = ~0ULL;
        for (uint64_t i = 0; i < kernel::Scheduler::task_count(); ++i) {
            auto* t = kernel::Scheduler::task_at(i);
            if (!t || t == kernel::Scheduler::task_at(0)) continue;
            if (t->state != kernel::TaskState::READY
             && t->state != kernel::TaskState::RUNNING) continue;
            if (!t->page_table_) continue;
            if (t->priority < victim_priority) {
                victim = t;
                victim_priority = t->priority;
            }
        }
        if (!victim) return false;
        debug_write("[OOM] Killing task ");
        debug_write_hex(victim->id);
        debug_write(" priority=");
        debug_write_hex(victim->priority);
        debug_write("\n");
        victim->state = kernel::TaskState::TERMINATED;
        victim->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(9));
        kernel::Scheduler::reap_orphans();
        return true;
    });
    kernel::sync::init_all();
    kernel::DriverRegistry::init();
    debug_write("[BOOT] Kernel init done\n");

    service::Framebuffer::init();
    service::Terminal::init();
    if (service::Terminal::instance()) {
        service::Terminal::show_splash();
    }

    init_pic();
    arch::Keyboard::init();
    arch::IDT::register_handler(
        arch::InterruptVector::KEYBOARD,
        [](uint64_t, uint64_t, uint64_t) {
            arch::Keyboard::handle_irq();
            outb(arch::PIC1_CMD, 0x20);
        }
    );

    kernel::vfs::devfs_init();
    kernel::vfs::mount(kernel::vfs::dev_fs, "/dev");
    kernel::vfs::mount(kernel::vfs::proc_fs, "/proc");
    kernel::vfs::mount(kernel::vfs::tmpfs_fs, "/tmp");

    // Probe AHCI controller first, fall back to legacy ATA PIO
    {
        auto* ahci = kernel::block::AhciDriver::probe();
        if (ahci) {
            debug_write("[BOOT] AHCI drive found\n");
            auto* part = new kernel::fat32::Fat32Partition(*ahci);
            if (part->mount()) {
                debug_write("[BOOT] FAT32 partition mounted via AHCI\n");
                kernel::vfs::fat32_partition_instance = part;
                if (kernel::vfs::mount_fat32("/mnt") == 0) {
                    debug_write("[BOOT] FAT32 filesystem mounted at /mnt\n");
                } else {
                    debug_write("[BOOT] mount_fat32 failed\n");
                }
            } else {
                debug_write("[BOOT] FAT32 partition mount failed\n");
                delete part;
            }
        } else {
            debug_write("[BOOT] No AHCI drive found, trying legacy ATA PIO\n");
            auto* ata = kernel::block::AtaPioDriver::probe_first_drive();
            if (ata) {
                debug_write("[BOOT] ATA drive found\n");
                auto* part = new kernel::fat32::Fat32Partition(*ata);
                if (part->mount()) {
                    debug_write("[BOOT] FAT32 partition mounted via PIO\n");
                    kernel::vfs::fat32_partition_instance = part;
                    if (kernel::vfs::mount_fat32("/mnt") == 0) {
                        debug_write("[BOOT] FAT32 filesystem mounted at /mnt\n");
                    } else {
                        debug_write("[BOOT] mount_fat32 failed\n");
                    }
                } else {
                    debug_write("[BOOT] FAT32 partition mount failed\n");
                    delete part;
                }
            } else {
                debug_write("[BOOT] No ATA drive found\n");
            }
        }
    }

    // Probe virtio-net NIC and initialize network stack
    {
        static net::Nic g_boot_nic;
        if (kernel::net::virtio_net_probe(g_boot_nic)) {
            net::net_init(g_boot_nic, g_boot_nic.mac,
                          net::Ipv4Addr{{10, 0, 2, 15}},
                          net::Ipv4Addr{{255, 255, 255, 0}},
                          net::Ipv4Addr{{10, 0, 2, 2}});
            debug_write("[BOOT] Virtio-net NIC probed, IP=10.0.2.15\n");
        } else {
            debug_write("[BOOT] No virtio-net NIC found\n");
        }
    }

    arch::Timer::init(kernel::BootParams::instance().timer_hz);
    arch::RTC::init();
    kernel::random_init();
    debug_write("[BOOT] Hardware init done\n");

    kernel::DriverRegistry::register_driver(
        "keyboard", "PS/2 Tastaturtreiber", nullptr, nullptr, 1);
    kernel::DriverRegistry::register_driver(
        "timer", "PIT Timer (1000 Hz)", nullptr, nullptr, 0);
    kernel::DriverRegistry::register_driver(
        "framebuffer", "Framebuffer Grafiktreiber", nullptr, nullptr, 0);
    kernel::DriverRegistry::register_driver(
        "pcspkr", "PC Speaker Soundtreiber", nullptr, nullptr, 0);

    service::ProgramRegistry::init();
    service::ProgramRegistry::register_program(
        "demo", "Mandelbrot set + spinning rectangles (framebuffer)", programs::
            demo_main);

    kernel::BufferPool::init();
    kernel::daemon::init();

// Load vfsd userspace daemon before test suite so tests can interact with it
    {
        initrd::InitrdFile f = initrd::find("./vfsd.c.elf");
        if (!f.data) f = initrd::find("vfsd.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data
                );
            if (kernel::elf::validate_header(hdr)) {
                auto* vfsd_task = kernel::elf::load(hdr, f.data);
                if (vfsd_task) {
                    vfsd_task->priority = 1;
                    vfsd_task->period_ticks = 10;
                    vfsd_task->init_sporadic_server(2, 10, 0);
                    kernel::Scheduler::add_task(*vfsd_task);
                    kernel::vfsd::set_vfsd_pid(vfsd_task->id);
                    kernel::daemon::register_daemon(
                        "vfsd", "vfsd.c.elf",
                        kernel::vfsd::set_vfsd_pid,
                        kernel::vfsd::get_vfsd_pid);
                }
            }
        }
    }

    // Load iocd userspace daemon before test suite
    {
        initrd::InitrdFile f = initrd::find("./iocd.c.elf");
        if (!f.data) f = initrd::find("iocd.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data
                );
            if (kernel::elf::validate_header(hdr)) {
                auto* iocd_task = kernel::elf::load(hdr, f.data);
                if (iocd_task) {
                    iocd_task->priority = 1;
                    iocd_task->period_ticks = 10;
                    iocd_task->init_sporadic_server(3, 10, 0);
                    kernel::Scheduler::add_task(*iocd_task);
                    kernel::iocd::set_iocd_pid(iocd_task->id);
                    kernel::daemon::register_daemon(
                        "iocd", "iocd.c.elf",
                        kernel::iocd::set_iocd_pid,
                        kernel::iocd::get_iocd_pid);
                }
            }
        }
    }

    if (service::Terminal::instance()) {
        service::Terminal::set_fb_enabled(false);
    }

    kernel::test::Registry::init();
    kernel::test::parse_test_config("./tests/test-config.txt");

    const char** classes = kernel::test::get_test_classes();
    size_t class_count = kernel::test::get_test_class_count();
    for (size_t i = 0; i < class_count; ++i) {
        kernel::test::register_class(classes[i]);
    }

#ifdef CONFIG_DEBUG
    kernel::Logger::info("[TEST] Registry tests=%u classes=%u", (unsigned)kernel::test::Registry::count(), (unsigned)kernel::test::Registry::class_count());
    kernel::test::set_class_auto_shutdown(true);
    kernel::test::run_registered(0);
#else
    kernel::test::set_class_auto_shutdown(false);
    kernel::test::run_filtered(kernel::test::TF_RELEASE, false);
#endif

    if (service::Terminal::instance()) {
        service::Terminal::set_fb_enabled(true);
    }

    {
        initrd::InitrdFile f = initrd::find("./test_fork.c.elf");
        if (!f.data) f = initrd::find("test_fork.c.elf");
        if (f.data) {
            auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data
                );
            if (kernel::elf::validate_header(hdr)) {
                auto* test_task = kernel::elf::load(hdr, f.data);
                if (test_task) {
                    test_task->priority = 1;
                    test_task->period_ticks = 50;
                    kernel::Scheduler::add_task(*test_task);
                }
            }
        }
    }

    // Kernel shell (main interactive shell — bypasses VFS daemon)
    service::Shell::init();
    {
        auto* ksh = kernel::TaskControlBlock::create(
            &service::Shell::shell_task_main, 2, 5);
        if (ksh) {
            kernel::Scheduler::add_task(*ksh);
            kernel::Scheduler::set_shell_task(ksh);
        }
    }

#ifdef CONFIG_PROFILING
    gcov_flush_to_serial();
    arch::qemu_debug_exit(0);
#endif

    debug_write("[BOOT] Boot complete, enabling interrupts\n");
    sti();
    for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) {
        arch::hlt();
    }
    __builtin_unreachable();
}

extern "C" void panic(const char* msg) {
    cli();
    kernel::Logger::fatal("KERNEL PANIC: %s", msg);
    if (service::Terminal::instance()) {
        service::Terminal::set_fg(0xFF0000);
        service::Terminal::write("\nKERNEL PANIC: ");
        service::Terminal::write(msg);
        service::Terminal::set_fg(0xC0C0C0);
    }
    for (uint64_t _i = 0; _i < UINT64_MAX; ++_i) {
        arch::hlt();
    }
    __builtin_unreachable();
}

static void dump_regs(uint64_t* regs) {
    if (!regs) return;

    using L = kernel::Logger;

    L::raw_write("  RAX: "); L::print_hex(regs[0]);
    L::raw_write("  RBX: "); L::print_hex(regs[1]);
    L::raw_write("\n");
    L::raw_write("  RCX: "); L::print_hex(regs[2]);
    L::raw_write("  RDI: "); L::print_hex(regs[5]);
    L::raw_write("\n");
    L::raw_write("  RDX: "); L::print_hex(regs[3]);
    L::raw_write("  RSI: "); L::print_hex(regs[4]);
    L::raw_write("\n");
    L::raw_write("  RBP: "); L::print_hex(regs[6]);
    L::raw_write("  RSP: "); L::print_hex(reinterpret_cast<uint64_t>(&regs));
    L::raw_write("\n");
    L::raw_write("  R8:  "); L::print_hex(regs[7]);
    L::raw_write("  R9:  "); L::print_hex(regs[8]);
    L::raw_write("\n");
    L::raw_write("  R10: "); L::print_hex(regs[9]);
    L::raw_write("  R11: "); L::print_hex(regs[10]);
    L::raw_write("\n");
    L::raw_write("  R12: "); L::print_hex(regs[11]);
    L::raw_write("  R13: "); L::print_hex(regs[12]);
    L::raw_write("\n");
    L::raw_write("  R14: "); L::print_hex(regs[13]);
    L::raw_write("  R15: "); L::print_hex(regs[14]);
    L::raw_write("\n");
    L::raw_write("  RIP: "); L::print_hex(regs[17]);
    L::raw_write("  CS:  "); L::print_hex(regs[18]);
    L::raw_write("\n");
    L::raw_write("  RFL: "); L::print_hex(regs[19]);
    L::raw_write("\n");

    uint64_t cr0 = read_cr0();
    uint64_t cr2 = read_cr2();
    uint64_t cr3 = read_cr3();
    uint64_t cr4 = read_cr4();

    L::raw_write("  CR0: "); L::print_hex(cr0); L::raw_write("\n");
    L::raw_write("  CR2: "); L::print_hex(cr2);
    L::raw_write("  CR3: "); L::print_hex(cr3); L::raw_write("\n");
    L::raw_write("  CR4: "); L::print_hex(cr4); L::raw_write("\n");

    L::raw_write("  Stack trace:\n");
    uint64_t* rbp = reinterpret_cast<uint64_t*>(regs[6]);
    for (int i = 0; i < 8 && rbp && rbp[1]; ++i) {
        L::raw_write("    ["); L::print_dec(i); L::raw_write("] ");
        L::print_hex(rbp[1]);
        L::raw_write("\n");
        rbp = reinterpret_cast<uint64_t*>(rbp[0]);
    }
}

static const char* exception_name(uint64_t vector) {
    switch (vector) {
    case 0:  return "Division by Zero";
    case 1:  return "Debug";
    case 2:  return "NMI";
    case 3:  return "Breakpoint";
    case 4:  return "Overflow";
    case 5:  return "Bound Range";
    case 6:  return "Invalid Opcode";
    case 7:  return "Device Not Available";
    case 8:  return "Double Fault";
    case 10: return "Invalid TSS";
    case 11: return "Segment Not Present";
    case 12: return "Stack Segment Fault";
    case 13: return "General Protection Fault";
    case 14: return "Page Fault";
    case 16: return "x87 FPU Error";
    case 17: return "Alignment Check";
    case 18: return "Machine Check";
    case 19: return "SIMD FP Exception";
    case 30: return "Security Exception";
    default: return "Reserved";
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0,
                                    uint64_t arg1, uint64_t arg2,
                                    uint64_t arg3, uint64_t* regs);

/// @brief Delivers a signal to a user task by setting up a signal frame on the user stack.
///        Modifies regs to return to the user's registered signal handler (or terminates
///        the task if no handler is registered and the default action is to terminate).
/// @return true if signal was delivered (handler will run), false if task was terminated.
static bool deliver_signal_to_user(kernel::TaskControlBlock* task,
                                   uint64_t sig, uint64_t vector,
                                   uint64_t error_code, uint64_t rip,
                                   uint64_t* regs)
{
    if (!task || !regs) return false;

    // SIGKILL is always fatal — cannot be caught or ignored
    if (kernel::signal_is_fatal(sig)) {
        kernel::Logger::error("Task %x: SIGKILL (fatal, no handler "
                              "allowed)", task->id);
        task->state = kernel::TaskState::TERMINATED;
        task->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
        return false;
    }

    // If the task has a registered handler, invoke it
    if (task->has_signal_handler(sig)) {
        kernel::Logger::info("Task %x: delivering signal %x to handler at %x",
            task->id, sig,
                reinterpret_cast<uint64_t>(task->get_signal_handler(sig)));

        uint64_t user_rsp = regs[20];  // current user RSP
        // Align stack: push SignalFrame
        user_rsp -= sizeof(kernel::SignalFrame);
        // Align to 16 bytes: (RSP + 8) % 16 == 0 at handler entry
        // After pushing SignalFrame, we adjust so that at handler entry:
        // RSP is 8 mod 16 (because call pushes return addr)
        user_rsp &= ~0xFULL;

        uint64_t frame_phys = kernel::VMM::virt_to_phys_in_pml4(
            user_rsp, task->page_table_);
        auto* frame = reinterpret_cast<kernel::SignalFrame*>(
            arch::HHDM_OFFSET + frame_phys);
        if (!frame) {
            // If we cannot write the signal frame (bad stack),
            // terminate
            kernel::Logger::error("Task %x: cannot write signal frame, "
                                  "terminating",
                                  task->id);
            task->state = kernel::TaskState::TERMINATED;
            task->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
            return false;
        }

        *frame = kernel::SignalFrame{
            .sig = sig,
            .saved_rip = regs[17],
            .saved_rsp = regs[20],
            .saved_rflags = regs[19],
            .saved_cs = regs[18],
            .saved_ss = regs[21],
            .reserved = {0, 0}
        };

        // Modify return context to go to the signal handler
        regs[5] = sig;                   // RDI = signal number
        regs[17] = reinterpret_cast<uint64_t>(
            task->get_signal_handler(sig));  // RIP = handler
        regs[20] = user_rsp;             // RSP = adjusted user stack
        // Clear direction flag and RF
        regs[19] = arch::RFLAGS_DEFAULT; // RFLAGS with IF set

        return true;
    }

    // No handler registered — check default action
    auto action = kernel::default_signal_action(sig);
    if (action == kernel::SignalAction::IGNORE) {
        kernel::Logger::warn("Task %x: signal %x ignored (default)",
                             task->id, sig);
        return true;  // ignored, resume execution
    }

    // Default is to terminate
    kernel::Logger::error("Task %x: unhandled signal %x "
                          "vector=%x rip=%x err=%x",
                          task->id, sig, vector, rip, error_code);
    if (vector == 14) {
        uint64_t cr2_val = read_cr2();
        kernel::Logger::error("  CR2=%x", cr2_val);
    }
    dump_regs(regs);
    task->state = kernel::TaskState::TERMINATED;
    task->exit_code = static_cast<uint64_t>(-static_cast<int64_t>(sig));
    return false;
}

extern "C" void handle_interrupt_c(uint64_t vector, uint64_t error_code,
    uint64_t rip,
                                   uint64_t* regs)
{
    // #NM (Device Not Available, vector 7) — lazy FPU/SSE context switch
    if (vector == 7) {
        auto* current = kernel::Scheduler::current_task();
        if (!current) {
            panic("#NM with no current task");
        }

        // Clear CR0.TS first so FNINIT/FXSAVE/FXRSTOR don't recursively #NM
        uint64_t cr0 = arch::read_cr0();
        cr0 &= ~(1ULL << 3);
        arch::write_cr0(cr0);

        // Save previous owner's FPU state
        auto* prev_fpu_owner = __atomic_load_n(&fpu_owner, __ATOMIC_ACQUIRE);
        if (prev_fpu_owner && prev_fpu_owner != current) {
            arch::fxsave(prev_fpu_owner->fpu_state);
        }

        // Restore or initialize for current task
        if (current->fpu_used) {
            arch::fxrstor(current->fpu_state);
        } else {
            arch::fninit();
            uint32_t default_mxcsr = 0x1F80;
            arch::ldmxcsr(default_mxcsr);
            current->fpu_used = true;
        }

        __atomic_store_n(&fpu_owner, current, __ATOMIC_RELEASE);
        return;
    }

    if (vector < 32) {
        auto* t = kernel::Scheduler::current_task();
        uint64_t cs = regs ? regs[18] : 0;
        bool from_user = (cs == arch::SEG_USER_CODE || cs == arch::SEG_USER_DATA
            );

        if (from_user && t) {
// Check fault recovery first: if we're inside a safe_copy_from_user zone,
            // redirect to the recovery IP instead of delivering a signal.
            if (kernel::g_user_access_recover_ip) {
                regs[17] = kernel::g_user_access_recover_ip;
                kernel::g_user_access_recover_ip = 0;
                return;
            }

            auto mapping = kernel::exception_to_signal(vector);
            uint64_t sig = static_cast<uint64_t>(mapping.signal);
            kernel::Logger::warn("Task %x: exception vector=%x (%s) "
                                 "→ signal %x",
                                 t->id, vector, mapping.name, sig);

            if (vector == 14) {
                kernel::Logger::error("  CR2=%x", read_cr2());
            }

            bool was_delivered = deliver_signal_to_user(t, sig, vector,
                error_code, rip, regs);
            if (!was_delivered && t->state == kernel::TaskState::TERMINATED) {
                // If task was terminated, reschedule on return
                kernel::Scheduler::reschedule();
            }
            return;
        }

        kernel::Logger::fatal("CPU EXCEPTION: %s (vector=%x err=%x rip=%x)",
            exception_name(vector), vector, error_code, rip);
        dump_regs(regs);

        if (t) {
            kernel::Logger::raw_write("  Task: id=");
            kernel::Logger::print_hex(t->id);
            kernel::Logger::raw_write(" prio=");
            kernel::Logger::print_dec(t->priority);
            kernel::Logger::raw_write(" state=");
            kernel::Logger::print_hex(static_cast<uint64_t>(t->state));
            kernel::Logger::raw_write("\n");
        }

        panic("CPU EXCEPTION");
        return;
    }

    if (vector == 0x80) {
        regs[0] = syscall_handler(regs[0], regs[1], regs[2], regs[3], regs[4],
            regs);
        auto* task = kernel::Scheduler::current_task();

        // After syscall, check for pending signals on the current task
        if (task && task->pending_signals && task->page_table_ != 0) {
            // Find the highest-priority pending signal
            uint64_t sig = __builtin_ctzll(task->pending_signals);
             if (sig < 32) {
                 kernel::Logger::debug("Task %x: pending signal %x "
                                       "after syscall", task->id, sig);
                deliver_signal_to_user(task, sig, 0, 0, regs[17], regs);
                task->pending_signals &= ~(1ULL << sig);
            }
        }

        if (task && (task->state == kernel::TaskState::TERMINATED ||
                     task->state == kernel::TaskState::BLOCKED)) {
            kernel::Scheduler::reschedule();
        }
        return;
    }

    arch::IDT::handle_interrupt(vector, error_code, rip);

    if (vector >= 32 && vector < 48) {
        outb(arch::PIC1_CMD, 0x20);
        if (vector >= 40) outb(arch::PIC2_CMD, 0x20);
    }
}

extern "C" uint64_t syscall_handler(uint64_t number, uint64_t arg0,
    uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3, uint64_t* regs
                                        )
{
    return kernel::Syscall::handle(number, arg0, arg1, arg2, arg3, regs);
}

uint8_t kernel_stack[16_KiB] __attribute__((section(".boot_stack")));
