#include <services/shell.hpp>
#include <services/terminal/terminal.hpp>
#include <services/program.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <initrd/initrd.hpp>
#include <version.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/arch/keyboard.hpp>
#include <kernel/test/test_selftest.hpp>
#include <test.hpp>
#include <string.hpp>
#include <constants.hpp>

namespace service {

static void background_task_wrapper() {
    auto* task = kernel::Scheduler::current_task();
    auto* entry = task ? reinterpret_cast<void (*)()>(task->user_data) : nullptr;
    if (entry) entry();
    if (task) task->state = kernel::TaskState::TERMINATED;
    while (true) arch::hlt();
}

Shell::Command Shell::commands_[MAX_COMMANDS] = {};
size_t Shell::num_commands_ = 0;
bool Shell::initialized_ = false;
char Shell::work_dir_[BUF_SIZE] = {};

void Shell::init() {
    if (initialized_) return;

    register_command("help",    "Show available commands",          cmd_help);
    register_command("clear",   "Clear terminal screen",            cmd_clear);
    register_command("echo",    "Echo text to terminal",            cmd_echo);
    register_command("uptime",  "Show system uptime",               cmd_uptime);
    register_command("tasks",   "List running tasks",               cmd_tasks);
    register_command("meminfo", "Show memory usage",                cmd_meminfo);
    register_command("reboot",  "Reboot the system",                cmd_reboot);
    register_command("run",     "Run a registered program",         cmd_run);
    register_command("version", "Show kernel version info",         cmd_version);
    register_command("jobs",    "List background tasks",            cmd_jobs);
    register_command("modprobe","Load/init a kernel driver",        cmd_modprobe);
    register_command("modlist", "List available kernel drivers",    cmd_modlist);
    register_command("listprog","List registered programs",         cmd_listprog);
    register_command("cd",      "Change working directory",          cmd_cd);
    register_command("export",  "Set environment variable",          cmd_export);
    register_command("runelf",  "Run userspace ELF from initrd",     cmd_runelf);
    register_command("exit",    "Shut down the system",              cmd_exit);
    register_command("shutdown","Shut down the system",              cmd_exit);
    register_command("selftest","Run kernel self-tests",            cmd_selftest);

    work_dir_[0] = '/';
    work_dir_[1] = '\0';

    initialized_ = true;
}

void Shell::register_command(const char* name, const char* help, CommandFunc func) {
    if (num_commands_ >= MAX_COMMANDS) return;
    commands_[num_commands_].name = name;
    commands_[num_commands_].help = help;
    commands_[num_commands_].func = func;
    ++num_commands_;
}

static int str_cmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

void Shell::parse_and_exec(const char* line) {
    while (*line == ' ') ++line;
    if (!*line) return;

    size_t len = 0;
    const char* p = line;
    while (*p) { ++len; ++p; }

    char* buf = new char[len + 1];
    if (!buf) return;
    memcpy(buf, line, len + 1);

    const char* argv[MAX_ARGS];
    int argc = 0;
    char* ptr = buf;

    while (*ptr && static_cast<size_t>(argc) < MAX_ARGS) {
        while (*ptr == ' ') ++ptr;
        if (!*ptr) break;
        argv[argc++] = ptr;
        while (*ptr && *ptr != ' ') ++ptr;
        if (*ptr) { *ptr++ = '\0'; }
    }

    if (argc == 0) {
        delete[] buf;
        return;
    }

    for (size_t i = 0; i < num_commands_; ++i) {
        if (str_cmp(argv[0], commands_[i].name) == 0) {
            commands_[i].func(argc, argv);
            delete[] buf;
            return;
        }
    }

    Terminal::set_fg(0xFF4444);
    Terminal::write("Unbekannter Befehl: ");
    Terminal::write(argv[0]);
    Terminal::write("\n");
    Terminal::set_fg(0xC0C0C0);
    Terminal::write("Verfuegbar: help\n");

    delete[] buf;
}

void Shell::execute(const char* cmd) {
    if (!initialized_) init();
    parse_and_exec(cmd);
}

static void debug_putchar(char c) {
    if (c == '\n') { do { } while ((arch::inb(arch::COM1_LSR) & 0x20) == 0); arch::outb(arch::COM1,
    '\r'); }
    do { } while ((arch::inb(arch::COM1_LSR) & 0x20) == 0); arch::outb(arch::COM1, c);
}
static void debug_write(const char* s) { while (*s) debug_putchar(*s++); }

static bool readline(char* buf, size_t max_len) {
    size_t pos = 0;
    for (;;) {
        char c = 0;
        bool got_char = false;

        if (arch::inb(arch::COM1_LSR) & 1) {
            c = arch::inb(arch::COM1);
            got_char = true;
        }

        if (!got_char) {
            got_char = arch::Keyboard::getchar(c);
        }

        if (!got_char) {
            arch::pause();
            continue;
        }

        if (c == '\r') c = '\n';

        if (c == '\n') {
            Terminal::putchar('\n');
            buf[pos] = '\0';
            return true;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) { --pos; Terminal::putchar('\b'); }
            continue;
        }
        if (pos < max_len - 1) {
            buf[pos++] = c;
            Terminal::putchar(c);
        }
    }
}

void Shell::shell_task_main() {
    debug_write("[SHELL] Task started!\n");
    if (!initialized_) init();

    Terminal::set_fg(0x00FF00);
    Terminal::write("\n  ===== ");
    Terminal::write(kernel::Version::full_string());
    Terminal::write(" =====\n");
    Terminal::set_fg(0xC0C0C0);
    Terminal::write("  Mikrokernel + Rate Monotonic Scheduler\n");
    Terminal::write("  Geben Sie 'help' ein fuer Befehle\n\n");

    char line[BUF_SIZE];

    while (true) {
        Terminal::set_fg(0x00AAFF);
        Terminal::write("jarvis$ ");
        Terminal::set_fg(0xC0C0C0);

        arch::Keyboard::flush();
        if (readline(line, BUF_SIZE)) {
            parse_and_exec(line);
        }
    }
}

void Shell::cmd_help(int, const char**) {
    Terminal::write("\nVerfuegbare Befehle:\n");
    for (size_t i = 0; i < num_commands_; ++i) {
        Terminal::write("  ");
        Terminal::set_fg(0x00AAFF);
        Terminal::write(commands_[i].name);
        Terminal::set_fg(0xC0C0C0);

        size_t name_len = 0;
        const char* n = commands_[i].name;
        while (*n++) ++name_len;
        for (size_t p = name_len; p < 14; ++p) Terminal::putchar(' ');
        Terminal::write(commands_[i].help);
        Terminal::putchar('\n');
    }
    Terminal::putchar('\n');
}

void Shell::cmd_clear(int, const char**) {
    Terminal::clear();
}

void Shell::cmd_echo(int argc, const char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (i > 1) Terminal::putchar(' ');
        Terminal::write(argv[i]);
    }
    Terminal::putchar('\n');
}

void Shell::cmd_uptime(int, const char**) {
    uint64_t ticks = arch::Timer::ticks();
    uint64_t secs = ticks / 1000;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    mins %= 60;
    secs %= 60;

    auto print2 = [](uint64_t n) {
        Terminal::putchar('0' + n / 10);
        Terminal::putchar('0' + n % 10);
    };

    Terminal::write("Uptime: ");
    print2(hours);
    Terminal::putchar(':');
    print2(mins);
    Terminal::putchar(':');
    print2(secs);
    Terminal::putchar('\n');
}

void Shell::cmd_tasks(int, const char**) {
    auto* cur = kernel::Scheduler::current_task();
    auto count = kernel::Scheduler::task_count();

    Terminal::write("Tasks: ");
    char buf[16];
    int pos = 0;
    uint64_t n = count;
    if (n == 0) { Terminal::putchar('0'); }
    else {
        while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; }
        while (pos > 0) Terminal::putchar(buf[--pos]);
    }
    Terminal::write(" (aktuell: ");
    if (cur) {
        uint64_t id = cur->id;
        pos = 0;
        while (id > 0) { buf[pos++] = '0' + (id % 10); id /= 10; }
        while (pos > 0) Terminal::putchar(buf[--pos]);
    }
    Terminal::write(")\n");
}

void Shell::cmd_meminfo(int, const char**) {
    uint64_t total = kernel::PMM::total_memory();
    uint64_t free = kernel::PMM::free_memory();
    uint64_t used = total - free;

    auto print_size = [](uint64_t bytes) {
        uint64_t mb = bytes / (1024 * 1024);
        uint64_t remainder = bytes % (1024 * 1024);
        uint64_t kb = remainder / 1024;

        char buf[16];
        int pos = 0;
        if (mb == 0) { Terminal::putchar('0'); }
        else {
            uint64_t m = mb;
            while (m > 0) { buf[pos++] = '0' + (m % 10); m /= 10; }
            while (pos > 0) Terminal::putchar(buf[--pos]);
        }
        Terminal::write(" MiB");
        if (kb > 0) {
            Terminal::write(" ");
            pos = 0;
            while (kb > 0) { buf[pos++] = '0' + (kb % 10); kb /= 10; }
            while (pos > 0) Terminal::putchar(buf[--pos]);
            Terminal::write(" KiB");
        }
    };

    Terminal::write("Gesamt: "); print_size(total); Terminal::putchar('\n');
    Terminal::write("Belegt: "); print_size(used); Terminal::putchar('\n');
    Terminal::write("Frei:   "); print_size(free); Terminal::putchar('\n');
}

void Shell::cmd_version(int, const char**) {
    Terminal::set_fg(0x00FF00);
    Terminal::write(kernel::Version::full_string());
    Terminal::write("\n");
    Terminal::set_fg(0xC0C0C0);
    Terminal::write("Kernel: ");
    Terminal::write(kernel::Version::string());
    Terminal::write("\nBuild:  ");
    Terminal::write(kernel::Version::build_date());
    Terminal::write(" ");
    Terminal::write(kernel::Version::build_time());
    Terminal::write("\n");
}

void Shell::cmd_jobs(int, const char**) {
    Terminal::write("Background tasks:\n");
    uint64_t count = kernel::Scheduler::task_count();
    uint64_t shown = 0;
    for (uint64_t i = 0; i < count; ++i) {
        auto* task = kernel::Scheduler::task_at(i);
        if (task && task->state == kernel::TaskState::TERMINATED) continue;
        if (task && task->priority > 0 && task != kernel::Scheduler::current_task()) {
            char buf[16];
            int pos = 0;
            uint64_t id = task->id;
            while (id > 0) { buf[pos++] = '0' + (id % 10); id /= 10; }
            while (pos > 0) Terminal::putchar(buf[--pos]);
            Terminal::write(": priority=");
            pos = 0; uint64_t p = task->priority;
            while (p > 0) { buf[pos++] = '0' + (p % 10); p /= 10; }
            while (pos > 0) Terminal::putchar(buf[--pos]);
            Terminal::putchar('\n');
            ++shown;
        }
    }
    if (shown == 0) Terminal::write("  (none)\n");
}

void Shell::cmd_modprobe(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: modprobe <driver>\n");
        return;
    }
    if (kernel::DriverRegistry::load(argv[1])) {
        auto* drv = kernel::DriverRegistry::find(argv[1]);
        Terminal::write("Driver geladen: ");
        Terminal::set_fg(0x00FF00);
        Terminal::write(drv ? drv->name : argv[1]);
        Terminal::set_fg(0xC0C0C0);
        Terminal::putchar('\n');
    } else {
        Terminal::set_fg(0xFF4444);
        Terminal::write("Fehler: Treiber nicht gefunden oder Init fehlgeschlagen: ");
        Terminal::write(argv[1]);
        Terminal::set_fg(0xC0C0C0);
        Terminal::putchar('\n');
    }
}

void Shell::cmd_modlist(int, const char**) {
    size_t count = kernel::DriverRegistry::count();
    Terminal::write("Verfuegbare Treiber:\n");
    if (count == 0) { Terminal::write("  (keine)\n"); return; }
    for (size_t i = 0; i < count; ++i) {
        auto* drv = kernel::DriverRegistry::get(i);
        if (!drv) continue;
        Terminal::write("  ");
        Terminal::set_fg(0x00AAFF);
        Terminal::write(drv->name);
        Terminal::set_fg(0xC0C0C0);
        size_t nlen = 0;
        while (drv->name[nlen]) ++nlen;
        for (size_t p = nlen; p < 16; ++p) Terminal::putchar(' ');
        Terminal::write(drv->description);
        Terminal::write(" [");
        switch (drv->state) {
        case kernel::DriverState::LOADED:   Terminal::set_fg(0x00FF00); Terminal::write("geladen"); break;
        case kernel::DriverState::UNLOADED: Terminal::set_fg(0xFFFF00); Terminal::write("verfuegbar"); break;
        case kernel::DriverState::ERROR:    Terminal::set_fg(0xFF0000); Terminal::write("fehler"); break;
        }
        Terminal::set_fg(0xC0C0C0);
        Terminal::write("]\n");
    }
}

void Shell::cmd_reboot(int, const char**) {
    Terminal::write("Reboot...\n");
    uint8_t good;
    do {
        good = arch::inb(0x64);
    } while (good & 0x02);
    arch::outb(0x64, 0xFE);
    arch::hlt();
}

void Shell::cmd_run(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: run <program> [&]\n");
        Terminal::write("Verfuegbare Programme:\n");
        for (size_t i = 0; i < ProgramRegistry::count(); ++i) {
            auto* prog = ProgramRegistry::get(i);
            if (prog) {
                Terminal::write("  ");
                Terminal::write(prog->name);
                Terminal::write(" - ");
                Terminal::write(prog->description);
                Terminal::putchar('\n');
            }
        }
        return;
    }

    bool background = false;
    if (argc > 2 && str_cmp(argv[argc - 1], "&") == 0) {
        background = true;
        --argc;
    }

    auto* prog = ProgramRegistry::find(argv[1]);
    if (!prog) {
        Terminal::write("Programm nicht gefunden: ");
        Terminal::write(argv[1]);
        Terminal::putchar('\n');
        return;
    }

    if (background) {
        auto* task = kernel::TaskControlBlock::create(
            background_task_wrapper, 1, 100);
        if (task) task->user_data = reinterpret_cast<void*>(prog->entry);
        if (task) {
            kernel::Scheduler::add_task(*task);
            Terminal::write("Task #");
            char buf[16];
            int pos = 0;
            uint64_t id = task->id;
            while (id > 0) { buf[pos++] = '0' + (id % 10); id /= 10; }
            while (pos > 0) Terminal::putchar(buf[--pos]);
            Terminal::write(": ");
            Terminal::write(prog->name);
            Terminal::write(" (background)\n");
        }
        return;
    }

    Terminal::write("Starte: ");
    Terminal::write(prog->name);
    Terminal::putchar('\n');

    prog->entry();
}

void Shell::cmd_listprog(int, const char**) {
    Terminal::write("Registrierte Programme:\n");
    for (size_t i = 0; i < ProgramRegistry::count(); ++i) {
        auto* prog = ProgramRegistry::get(i);
        if (prog) {
            Terminal::write("  ");
            Terminal::set_fg(0x00AAFF);
            Terminal::write(prog->name);
            Terminal::set_fg(0xC0C0C0);
            Terminal::write(" - ");
            Terminal::write(prog->description);
            Terminal::putchar('\n');
        }
    }
    if (ProgramRegistry::count() == 0) {
        Terminal::write("  (keine)\n");
    }
}

void Shell::cmd_cd(int argc, const char** argv) {
    const char* target = (argc < 2) ? "/" : argv[1];
    auto* vn = kernel::vfs::resolve(target);
    if (!vn) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("cd: no such directory: ");
        Terminal::write(target);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    if (!(vn->mode & kernel::vfs::S_IFDIR)) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("cd: not a directory: ");
        Terminal::write(target);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    auto* task = kernel::Scheduler::current_task();
    if (!task) return;
    task->cwd_vnode = vn;
    size_t i = 0;
    while (target[i] && i < 255) { task->cwd[i] = target[i]; ++i; }
    task->cwd[i] = '\0';
}

void Shell::cmd_export(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: export VAR=value\n");
        return;
    }
    Terminal::write("export: ");
    Terminal::write(argv[1]);
    Terminal::write("\n");
}

void Shell::cmd_runelf(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: runelf <path.elf> [args...]\n");
        return;
    }

    const char* path = argv[1];

    initrd::InitrdFile f = initrd::find(path);
    if (!f.data) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("runelf: file not found in initrd: ");
        Terminal::write(path);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    auto* hdr = reinterpret_cast<const kernel::elf::ELF64Header*>(f.data);
    if (!kernel::elf::validate_header(hdr)) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("runelf: invalid ELF: ");
        Terminal::write(path);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    auto* task = kernel::elf::load(hdr, f.data);
    if (!task) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("runelf: failed to load: ");
        Terminal::write(path);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    kernel::Scheduler::add_task(*task);

    Terminal::set_fg(0x00FF00);
    Terminal::write("Started task #");
    char buf[16];
    int pos = 0;
    uint64_t id = task->id;
    while (id > 0) { buf[pos++] = '0' + (id % 10); id /= 10; }
    while (pos > 0) Terminal::putchar(buf[--pos]);
    Terminal::write(": ");
    Terminal::write(path);
    Terminal::putchar('\n');
    Terminal::set_fg(0xC0C0C0);
}

void Shell::cmd_selftest(int, const char**) {
    Terminal::write("Running kernel self-tests...\n");
    // Disable framebuffer: test activity may corrupt kernel page-table entries
    // (tests allocate/free pages, map/unmap memory).  Any subsequent
    // framebuffer access could fault.  Serial (COM1) is safe.
    Terminal::set_fb_enabled(false);
    // Disable interrupts so the timer IRQ cannot dispatch test-created kernel
    // tasks (they would run, return, get reaped, and then the test would
    // access freed TCB memory in cleanup/delete — a use-after-free crash).
    bool irq_was = arch::interrupts_enabled();
    arch::cli();

    kernel::test::Registry::init();
    register_selftest_tests();
    kernel::test::run_safe();
    if (irq_was) {
        // Reap any terminated test tasks that accumulated while interrupts
        // were disabled (on_tick → reap_orphans never ran).
        kernel::Scheduler::reap_orphans();
        // Remove any zombie tasks whose TCBs have been freed but whose
        // entries remain in the scheduler table (e.g. tests that deleted
        // tasks without calling remove_task, or where find_task failed
        // due to hash-table tombstones).  The TCB magic field distinguishes
        // valid tasks from freed-and-reused memory.
        kernel::Scheduler::cleanup_zombies();

        // Clear stale scheduler context-switch globals.  Test code may have
        // called reschedule()/switch_to_task() while interrupts were disabled
        // (setting scheduler_save_rsp_to / scheduler_load_rsp_from), but the
        // ISR assembly never consumed them.  If left dangling, the next timer
        // IRQ would attempt to context-switch via stale pointers to freed
        // task memory → GPF at iretq.
        kernel::scheduler_save_rsp_to = nullptr;
        kernel::scheduler_load_rsp_from = 0;
        kernel::scheduler_load_cr3_from = 0;
        kernel::scheduler_next_task_id = 0;
        arch::sti();
    }
    Terminal::set_fb_enabled(true);
    Terminal::write("Self-tests complete.\n");
}

void Shell::cmd_exit(int, const char**) {
    Terminal::set_fg(0xFF4444);
    Terminal::write("\nShutting down...\n");
    Terminal::set_fg(0xC0C0C0);

    arch::outw(arch::QEMU_ACPI_PORT, 0x2000);
    arch::outw(arch::QEMU_SHUTDOWN_PORT, 0x2000);

    for (int i = 0; i < 100000000; ++i) arch::pause();
    Terminal::write("Shutdown failed. Halting.\n");
    arch::cli(); arch::hlt();
}

} // namespace service
