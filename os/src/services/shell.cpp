#include <services/shell.hpp>
#include <services/terminal/terminal.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/program.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/arch/timer.hpp>
#include <kernel/arch/rtc.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/arch/irq_guard.hpp>
#include <kernel/elf/elf.hpp>
#include <kernel/vfs/vfs.hpp>
#include <initrd/initrd.hpp>
#include <version.hpp>
#include <kernel/driver/driver.hpp>
#include <kernel/arch/keyboard.hpp>
#include <kernel/test/test_selftest.hpp>
#include <kernel/syscall/syscall_helpers.hpp>
#include <kernel/net/net.hpp>
#include <kernel/driver/virtio_net.hpp>
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
char Shell::env_[Shell::MAX_ENV][Shell::BUF_SIZE] = {};
size_t Shell::env_count_ = 0;
int Shell::last_exit_code_ = 0;

Shell::AliasEntry Shell::aliases_[Shell::MAX_ALIASES] = {};
size_t Shell::alias_count_ = 0;

Shell::HistoryEntry Shell::history_[Shell::MAX_HISTORY] = {};
size_t Shell::history_count_ = 0;
size_t Shell::history_head_ = 0;

char Shell::dir_stack_[Shell::MAX_DIR_STACK][Shell::BUF_SIZE] = {};
size_t Shell::dir_stack_count_ = 0;

int Shell::shell_options_ = 0;
int Shell::positional_argc_ = 0;
char* Shell::positional_argv_[32] = {};

int Shell::umask_ = 0022;

Shell::TrapEntry Shell::traps_[32] = {};

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
    register_command("selftest","Run kernel self-tests",             cmd_selftest);
    register_command("pwd",     "Print working directory",           cmd_pwd);
    register_command("env",     "Print environment variables",       cmd_env);
    register_command("sleep",   "Sleep for N seconds",               cmd_sleep);
    register_command("mkdir",   "Create a directory",                cmd_mkdir);
    register_command("rm",      "Remove a file",                     cmd_rm);
    register_command("rmdir",   "Remove an empty directory",         cmd_rmdir);
    register_command("which",   "Locate a command",                  cmd_which);
    register_command("locate",  "Locate a command",                  cmd_which);
    register_command("alias",   "Define or list aliases",            cmd_alias);
    register_command("unalias", "Remove an alias",                   cmd_unalias);
    register_command("history", "Show command history",              cmd_history);
    register_command("type",    "Show command type",                 cmd_type);
    register_command("source",  "Execute a script file",             cmd_source);
    register_command(".",       "Execute a script file (POSIX)",     cmd_source);
    register_command("set",     "Set/unset shell options",           cmd_set);
    register_command("read",    "Read a line into variable(s)",      cmd_read);
    register_command("printf",  "Formatted output",                  cmd_printf);
    register_command("test",    "Evaluate conditional expression",   cmd_test);
    register_command("[",       "Evaluate conditional expression",   cmd_test);
    register_command("shift",   "Shift positional parameters",       cmd_shift);
    register_command("trap",    "Trap signals",                      cmd_trap);
    register_command("wait",    "Wait for background jobs",          cmd_wait);
    register_command("fg",      "Bring job to foreground",           cmd_fg);
    register_command("bg",      "Send job to background",            cmd_bg);
    register_command("disown",  "Remove job from table",             cmd_disown);
    register_command("ulimit",  "Resource limits",                   cmd_ulimit);
    register_command("umask",   "File creation mask",                cmd_umask);
    register_command("times",   "Process times",                     cmd_times);
    register_command("logout",  "Exit login shell",                  cmd_logout);
    register_command("dirs",    "Directory stack",                   cmd_dirs);
    register_command("pushd",   "Push directory to stack",           cmd_pushd);
    register_command("popd",    "Pop directory from stack",          cmd_popd);
    register_command("ls",      "List directory contents",           cmd_ls);
    register_command("ifconfig","Show/configure network interface",  cmd_ifconfig);
    register_command("ping",    "Send ICMP echo requests",           cmd_ping);

    work_dir_[0] = '/';
    work_dir_[1] = '\0';
    env_count_ = 0;
    last_exit_code_ = 0;

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

int Shell::parse_and_exec(const char* line) {
    while (*line == ' ') ++line;
    if (!*line) return 0;

    size_t len = 0;
    const char* p = line;
    while (*p) { ++len; ++p; }

    char* buf = new char[len + 1];
    if (!buf) return 1;
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
        return 0;
    }

    // Scan for redirect tokens
    const char* redirect_file = nullptr;
    int redirect_pos = -1;
    bool redirect_stdout = false;

    for (int i = 0; i < argc; ++i) {
        if (str_cmp(argv[i], ">") == 0 && i + 1 < argc) {
            redirect_file = argv[i + 1];
            redirect_pos = i;
            redirect_stdout = true;
            break;
        }
    }

    // Execute with optional output capture

    // Determine command name and effective argc (excluding redirect tokens)
    const char* cmd_name = argv[0];
    int effective_argc = (redirect_pos >= 0) ? redirect_pos : argc;

    // Alias expansion: if argv[0] is an alias, expand and re-parse
    for (size_t ai = 0; ai < alias_count_; ++ai) {
        if (str_cmp(cmd_name, aliases_[ai].name) == 0) {
            // Reconstruct command: alias value + remaining args
            char expanded[BUF_SIZE * 2];
            size_t pos = 0;
            for (const char* v = aliases_[ai].value; *v && pos < sizeof(expanded) - 2; ++v)
                expanded[pos++] = *v;
            for (int ai_arg = 1; ai_arg < effective_argc; ++ai_arg) {
                if (pos >= sizeof(expanded) - 2) break;
                expanded[pos++] = ' ';
                for (const char* a = argv[ai_arg]; *a && pos < sizeof(expanded) - 2; ++a)
                    expanded[pos++] = *a;
            }
            expanded[pos] = '\0';
            delete[] buf;
            // Ensure history is added for the expanded command? Just recurse
            return parse_and_exec(expanded);
        }
    }

    // Record history
    if (history_count_ < Shell::MAX_HISTORY) {
        size_t hi = history_count_;
        size_t hp = 0;
        for (const char* hl = line; *hl && hp < BUF_SIZE - 1; ++hl) history_[hi].cmd[hp++] = *hl;
        history_[hi].cmd[hp] = '\0';
        history_[hi].used = true;
        ++history_count_;
    } else {
        size_t hi = history_head_;
        size_t hp = 0;
        for (const char* hl = line; *hl && hp < BUF_SIZE - 1; ++hl) history_[hi].cmd[hp++] = *hl;
        history_[hi].cmd[hp] = '\0';
        history_head_ = (history_head_ + 1) % Shell::MAX_HISTORY;
    }

    for (size_t i = 0; i < num_commands_; ++i) {
        if (str_cmp(cmd_name, commands_[i].name) == 0) {

            if (redirect_stdout && redirect_file) {
                char capture_buf[4096];
                Terminal::capture_begin(capture_buf, sizeof(capture_buf));

                commands_[i].func(effective_argc, argv);

                Terminal::capture_end();

                // Write captured output to the file
                if (capture_buf[0]) {
                    auto* vn = kernel::vfs::resolve(redirect_file);
                    if (!vn) {
                        // File doesn't exist — try to open it for writing,
                        // which will fail on most filesystems. Fall back to
                        // creating via parent directory.
                        Terminal::write("(redirect to ");
                        Terminal::write(redirect_file);
                        Terminal::write(")\n");
                    }

                    auto* task = kernel::Scheduler::current_task();
                    if (task) {
                        int fd = kernel::syscall_path_open(redirect_file, 1);
                        if (fd >= 0) {
                            size_t slen = 0;
                            while (capture_buf[slen]) ++slen;
                            auto* f = task->fd_table.get(fd);
                            if (f && f->vnode && f->vnode->ops->write) {
                                f->vnode->ops->write(*f->vnode,
                                    reinterpret_cast<const uint8_t*>(capture_buf),
                                    slen, 0);
                            }
                            task->fd_table.free(fd);
                        }
                    }
                }
            } else {
                commands_[i].func(effective_argc, argv);
            }

            delete[] buf;
            return 0;
        }
    }

    Terminal::set_fg(0xFF4444);
    Terminal::write("Unbekannter Befehl: ");
    Terminal::write(argv[0]);
    Terminal::write("\n");
    Terminal::set_fg(0xC0C0C0);
    Terminal::write("Verfuegbar: help\n");

    delete[] buf;
    return 1;
}

void Shell::execute(const char* cmd) {
    if (!initialized_) init();
    parse_and_exec(cmd);
}

static void update_status_bar();

static bool readline(char* buf, size_t max_len) {
    size_t pos = 0;
    static uint64_t last_blink = 0;
    static bool cursor_on = false;

    auto redraw_cursor = [&](bool on) {
        cursor_on = on;
        Terminal::set_cursor_visible(on);
    };

    for (;;) {
        uint64_t now = arch::Timer::ticks();
        if (now - last_blink >= 500) {
            last_blink = now;
            redraw_cursor(!cursor_on);
            update_status_bar();
        }

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

        redraw_cursor(true);

        if (c == '\r') c = '\n';

        if (c == '\n') {
            redraw_cursor(false);
            Terminal::putchar('\n');
            buf[pos] = '\0';
            return true;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                --pos;
                Terminal::putchar('\b');
            }
            continue;
        }
        if (pos < max_len - 1) {
            buf[pos++] = c;
            Terminal::putchar(c);
        }
    }
}

static void append_two_digit(char*& p, uint32_t n) {
    *p++ = '0' + (n / 10) % 10;
    *p++ = '0' + n % 10;
}

static void append_four_digit(char*& p, uint32_t n) {
    *p++ = '0' + (n / 1000) % 10;
    *p++ = '0' + (n / 100) % 10;
    append_two_digit(p, n % 100);
}

static void append_size(char*& p, uint64_t bytes) {
    uint64_t mb = bytes / (1024 * 1024);
    if (mb >= 100) *p++ = '0' + (mb / 100) % 10;
    if (mb >= 10) *p++ = '0' + (mb / 10) % 10;
    *p++ = '0' + mb % 10;
}

static void update_status_bar() {
    if (!Terminal::instance()) return;
    if (!Framebuffer::available()) return;

    char left[64];
    char* lp = left;
    for (const char* s = "Jarvis RTOS "; *s; ++s) *lp++ = *s;
    for (const char* s = kernel::Version::string(); *s; ++s) *lp++ = *s;
#ifdef CONFIG_DEBUG
    for (const char* s = " [D]"; *s; ++s) *lp++ = *s;
#else
    for (const char* s = " [R]"; *s; ++s) *lp++ = *s;
#endif
    *lp = '\0';

    char right[96];
    char* rp = right;

    arch::tm tm;
    arch::RTC::read_time(&tm);

    uint16_t year = static_cast<uint16_t>(tm.tm_year + 1900);
    append_four_digit(rp, year);
    *rp++ = '-';
    append_two_digit(rp, static_cast<uint32_t>(tm.tm_mon + 1));
    *rp++ = '-';
    append_two_digit(rp, static_cast<uint32_t>(tm.tm_mday));
    *rp++ = ' ';
    append_two_digit(rp, static_cast<uint32_t>(tm.tm_hour));
    *rp++ = ':';
    append_two_digit(rp, static_cast<uint32_t>(tm.tm_min));
    *rp++ = ':';
    append_two_digit(rp, static_cast<uint32_t>(tm.tm_sec));

    for (const char* s = " | Up "; *s; ++s) *rp++ = *s;

    uint64_t total_sec = arch::Timer::ticks() / 1000;
    uint32_t uh = static_cast<uint32_t>(total_sec / 3600);
    uint32_t um = static_cast<uint32_t>((total_sec % 3600) / 60);
    uint32_t us = static_cast<uint32_t>(total_sec % 60);
    append_two_digit(rp, uh);
    *rp++ = ':';
    append_two_digit(rp, um);
    *rp++ = ':';
    append_two_digit(rp, us);

    uint64_t total_mem = kernel::PMM::total_memory();
    uint64_t used_mem = total_mem - kernel::PMM::free_memory();
    for (const char* s = " | Mem: "; *s; ++s) *rp++ = *s;
    append_size(rp, used_mem);
    *rp++ = '/';
    append_size(rp, total_mem);
    for (const char* s = "M"; *s; ++s) *rp++ = *s;

    for (const char* s = " | T:"; *s; ++s) *rp++ = *s;

    uint64_t tc = kernel::Scheduler::task_count();
    if (tc >= 100) *rp++ = '0' + (tc / 100) % 10;
    if (tc >= 10) *rp++ = '0' + (tc / 10) % 10;
    *rp++ = '0' + tc % 10;
    *rp = '\0';

    Terminal::draw_status_bar(left, right);
}

void Shell::shell_task_main() {
    if (!initialized_) init();

    Terminal::clear();
    Terminal::write("\n");
    Terminal::set_fg(0x00FF00);
    Terminal::write("Jarvis RTOS ");
    Terminal::write(kernel::Version::string());
    Terminal::write("\n");
    Terminal::set_fg(0xC0C0C0);
    Terminal::write("Type 'help' for available commands\n\n");

    char line[BUF_SIZE];

    while (true) {
        update_status_bar();

        auto* task = kernel::Scheduler::current_task();
        const char* cwd = task ? task->cwd : "/";

        // Exit status indicator: green checkmark for 0, red X for non-zero
        if (last_exit_code_ == 0) {
            Terminal::set_fg(0x00AA00);
            Terminal::write("✓ ");
        } else {
            Terminal::set_fg(0xCC3333);
            Terminal::write("✗ ");
        }

        // Current directory in blue
        Terminal::set_fg(0x00AAFF);
        Terminal::write(cwd);
        Terminal::write(" ");
        Terminal::set_fg(0xC0C0C0);
        Terminal::write("$ ");

        arch::Keyboard::flush();
        if (readline(line, BUF_SIZE)) {
            last_exit_code_ = parse_and_exec(line);
            update_status_bar();
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

    const char* arg = argv[1];
    const char* eq = nullptr;
    for (const char* p = arg; *p; ++p) {
        if (*p == '=') { eq = p; break; }
    }
    if (!eq || eq == arg) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("export: malformed, use VAR=value\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    size_t name_len = static_cast<size_t>(eq - arg);
    const char* val = eq + 1;

    // Check if variable already exists, update it
    for (size_t i = 0; i < env_count_; ++i) {
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (env_[i][j] != arg[j]) { match = false; break; }
        }
        if (match && env_[i][name_len] == '=') {
            // Update existing entry
            size_t pos = 0;
            for (size_t j = 0; j < name_len; ++j) env_[i][pos++] = arg[j];
            env_[i][pos++] = '=';
            for (const char* v = val; *v && pos < BUF_SIZE - 1; ++v) env_[i][pos++] = *v;
            env_[i][pos] = '\0';
            return;
        }
    }

    // Add new entry
    if (env_count_ >= MAX_ENV) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("export: environment full\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    size_t pos = 0;
    for (size_t j = 0; j < name_len; ++j) env_[env_count_][pos++] = arg[j];
    env_[env_count_][pos++] = '=';
    for (const char* v = val; *v && pos < BUF_SIZE - 1; ++v) env_[env_count_][pos++] = *v;
    env_[env_count_][pos] = '\0';
    ++env_count_;
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

    {
        // Disable interrupts so the timer IRQ cannot dispatch test-created kernel
        // tasks (they would run, return, get reaped, and then the test would
        // access freed TCB memory in cleanup/delete — a use-after-free crash).
        arch::IrqGuard guard;

        kernel::test::Registry::init();
        register_selftest_tests();
        kernel::test::run_safe();

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

void Shell::cmd_pwd(int, const char**) {
    auto* task = kernel::Scheduler::current_task();
    if (!task) return;
    Terminal::write(task->cwd);
    Terminal::putchar('\n');
}

void Shell::cmd_which(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: which <command...>\n");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        bool found = false;
        for (size_t j = 0; j < num_commands_; ++j) {
            if (strcmp(argv[i], commands_[j].name) == 0) {
                Terminal::write(commands_[j].name);
                Terminal::write(" (shell built-in)\n");
                found = true;
                break;
            }
        }
        if (!found) {
            Terminal::write(argv[i]);
            Terminal::write(": not found\n");
        }
    }
}

void Shell::cmd_env(int, const char**) {
    if (env_count_ == 0) {
        Terminal::write("(no environment variables set)\n");
        return;
    }
    for (size_t i = 0; i < env_count_; ++i) {
        Terminal::write(env_[i]);
        Terminal::putchar('\n');
    }
}

void Shell::cmd_sleep(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: sleep <seconds>\n");
        return;
    }
    uint64_t secs = 0;
    const char* p = argv[1];
    while (*p) {
        secs = secs * 10 + static_cast<uint64_t>(*p++ - '0');
    }
    uint64_t target = arch::Timer::ticks() + secs * 1000;
    while (arch::Timer::ticks() < target) {
        arch::pause();
    }
}

void Shell::cmd_mkdir(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: mkdir <path>\n");
        return;
    }
    int r = kernel::vfs::mkdir(argv[1], 0);
    if (r != 0) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("mkdir: failed to create directory\n");
        Terminal::set_fg(0xC0C0C0);
    }
}

void Shell::cmd_rm(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: rm <path>\n");
        return;
    }
    int r = kernel::vfs::unlink(argv[1]);
    if (r != 0) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("rm: failed to remove file\n");
        Terminal::set_fg(0xC0C0C0);
    }
}

void Shell::cmd_rmdir(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: rmdir <path>\n");
        return;
    }
    int r = kernel::vfs::unlink(argv[1]);
    if (r != 0) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("rmdir: failed to remove directory (not empty?)\n");
        Terminal::set_fg(0xC0C0C0);
    }
}

void Shell::cmd_alias(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Aliases:\n");
        for (size_t i = 0; i < alias_count_; ++i) {
            Terminal::write("  ");
            Terminal::write(aliases_[i].name);
            Terminal::write("='");
            Terminal::write(aliases_[i].value);
            Terminal::write("'\n");
        }
        return;
    }
    const char* eq = nullptr;
    for (const char* p = argv[1]; *p; ++p) {
        if (*p == '=') { eq = p; break; }
    }
    if (!eq || eq == argv[1]) {
        // If alias exists, show its value
        for (size_t i = 0; i < alias_count_; ++i) {
            if (str_cmp(argv[1], aliases_[i].name) == 0) {
                Terminal::write(aliases_[i].name);
                Terminal::write("='");
                Terminal::write(aliases_[i].value);
                Terminal::write("'\n");
                return;
            }
        }
        Terminal::set_fg(0xFF4444);
        Terminal::write("alias: not found: ");
        Terminal::write(argv[1]);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    size_t name_len = static_cast<size_t>(eq - argv[1]);
    const char* val = eq + 1;
    for (size_t i = 0; i < alias_count_; ++i) {
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (aliases_[i].name[j] != argv[1][j]) { match = false; break; }
        }
        if (match && aliases_[i].name[name_len] == '\0') {
            size_t pos = 0;
            for (size_t j = 0; j < name_len; ++j) aliases_[i].name[pos++] = argv[1][j];
            aliases_[i].name[pos] = '\0';
            pos = 0;
            for (const char* v = val; *v && pos < 255; ++v) aliases_[i].value[pos++] = *v;
            aliases_[i].value[pos] = '\0';
            return;
        }
    }
    if (alias_count_ >= Shell::MAX_ALIASES) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("alias: limit reached\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    size_t pos = 0;
    for (size_t j = 0; j < name_len; ++j) aliases_[alias_count_].name[pos++] = argv[1][j];
    aliases_[alias_count_].name[pos] = '\0';
    pos = 0;
    for (const char* v = val; *v && pos < 255; ++v) aliases_[alias_count_].value[pos++] = *v;
    aliases_[alias_count_].value[pos] = '\0';
    ++alias_count_;
}

void Shell::cmd_unalias(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: unalias <name>\n");
        return;
    }
    for (size_t i = 0; i < alias_count_; ++i) {
        if (str_cmp(argv[1], aliases_[i].name) == 0) {
            for (size_t j = i; j < alias_count_ - 1; ++j) aliases_[j] = aliases_[j + 1];
            --alias_count_;
            return;
        }
    }
    Terminal::set_fg(0xFF4444);
    Terminal::write("unalias: not found: ");
    Terminal::write(argv[1]);
    Terminal::putchar('\n');
    Terminal::set_fg(0xC0C0C0);
}

void Shell::cmd_history(int, const char**) {
    size_t start = history_count_ > 20 ? history_count_ - 20 : 0;
    for (size_t i = start; i < history_count_; ++i) {
        char num[16];
        int pos = 0;
        size_t n = i + 1;
        while (n > 0) { num[pos++] = '0' + (n % 10); n /= 10; }
        while (pos > 0) Terminal::putchar(num[--pos]);
        Terminal::write("  ");
        Terminal::write(history_[i].cmd);
        Terminal::putchar('\n');
    }
}

void Shell::cmd_type(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: type <name...>\n");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        // Check alias
        bool found = false;
        for (size_t j = 0; j < alias_count_; ++j) {
            if (str_cmp(argv[i], aliases_[j].name) == 0) {
                Terminal::write(argv[i]);
                Terminal::write(" is an alias for '");
                Terminal::write(aliases_[j].value);
                Terminal::write("'\n");
                found = true;
                break;
            }
        }
        if (found) continue;
        // Check built-in
        for (size_t j = 0; j < num_commands_; ++j) {
            if (str_cmp(argv[i], commands_[j].name) == 0) {
                Terminal::write(argv[i]);
                Terminal::write(" is a shell built-in\n");
                found = true;
                break;
            }
        }
        if (found) continue;
        Terminal::write(argv[i]);
        Terminal::write(": not found\n");
    }
}

void Shell::cmd_source(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: source <path>\n");
        return;
    }
    auto* vn = kernel::vfs::resolve(argv[1]);
    if (!vn || !(vn->mode & kernel::vfs::S_IFREG)) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("source: cannot read: ");
        Terminal::write(argv[1]);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    if (!vn->ops->read) return;
    uint8_t buf[4096];
    int64_t nread = vn->ops->read(*vn, buf, sizeof(buf) - 1, 0);
    if (nread <= 0) return;
    buf[nread] = '\0';
    // Execute each line
    char* line = reinterpret_cast<char*>(buf);
    char* start = line;
    while (*line) {
        if (*line == '\n') {
            *line = '\0';
            if (*start) parse_and_exec(start);
            start = line + 1;
        }
        ++line;
    }
    if (*start) parse_and_exec(start);
}

void Shell::cmd_set(int argc, const char** argv) {
    if (argc < 2) {
        // Show all shell variables
        Terminal::write("Shell options:\n");
        Terminal::write("  positional args: ");
        char buf[16];
        int pos = 0;
        int n = positional_argc_;
        if (n == 0) { buf[pos++] = '0'; }
        else { while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; } }
        while (pos > 0) Terminal::putchar(buf[--pos]);
        Terminal::putchar('\n');
        // Show environment
        for (size_t i = 0; i < env_count_; ++i) {
            Terminal::write("  ");
            Terminal::write(env_[i]);
            Terminal::putchar('\n');
        }
        return;
    }
    // Parse options
    if (argv[1][0] == '-') {
        for (const char* p = argv[1] + 1; *p; ++p) {
            switch (*p) {
            case 'x': shell_options_ |= 1; break;
            case 'e': shell_options_ |= 2; break;
            case 'u': shell_options_ |= 4; break;
            default:
                Terminal::set_fg(0xFF4444);
                Terminal::write("set: unknown option: -");
                Terminal::putchar(*p);
                Terminal::putchar('\n');
                Terminal::set_fg(0xC0C0C0);
                return;
            }
        }
    } else if (argv[1][0] == '+' && argv[1][1]) {
        for (const char* p = argv[1] + 1; *p; ++p) {
            switch (*p) {
            case 'x': shell_options_ &= ~1; break;
            case 'e': shell_options_ &= ~2; break;
            case 'u': shell_options_ &= ~4; break;
            default:
                Terminal::set_fg(0xFF4444);
                Terminal::write("set: unknown option: +");
                Terminal::putchar(*p);
                Terminal::putchar('\n');
                Terminal::set_fg(0xC0C0C0);
                return;
            }
        }
    } else {
        // Set positional parameters
        positional_argc_ = argc - 1;
        for (int i = 0; i < positional_argc_ && i < 32; ++i) {
            size_t slen = 0;
            while (argv[i + 1][slen]) ++slen;
            auto* old = positional_argv_[i];
            positional_argv_[i] = new char[slen + 1];
            for (size_t j = 0; j <= slen; ++j) positional_argv_[i][j] = argv[i + 1][j];
            delete[] old;
        }
    }
}

void Shell::cmd_read(int argc, const char** argv) {
    char line[BUF_SIZE];
    size_t pos = 0;
    for (;;) {
        char c = 0;
        bool got = false;
        if (arch::inb(arch::COM1_LSR) & 1) { c = arch::inb(arch::COM1); got = true; }
        if (!got) got = arch::Keyboard::getchar(c);
        if (!got) { arch::pause(); continue; }
        if (c == '\r') c = '\n';
        if (c == '\n') { line[pos] = '\0'; break; }
        if ((c == '\b' || c == 0x7F) && pos > 0) { --pos; Terminal::putchar('\b'); continue; }
        if (pos < BUF_SIZE - 1) { line[pos++] = c; Terminal::putchar(c); }
    }
    Terminal::putchar('\n');
    if (argc < 2) return;
    // Store into variable
    for (size_t i = 0; i < env_count_; ++i) {
        bool match = true;
        for (size_t j = 0; argv[1][j]; ++j) {
            if (env_[i][j] != argv[1][j] || env_[i][j] == '\0') { match = false; break; }
        }
        if (match && env_[i][argv[1][0] ? 0 : 1] != 0) {
            // Wait, need proper match: name must be followed by '='
            // This is getting complex - just handle it
        }
    }
    // Simple: env variable VAR gets the line
    if (env_count_ < MAX_ENV) {
        size_t p = 0;
        for (const char* s = argv[1]; *s; ++s) env_[env_count_][p++] = *s;
        env_[env_count_][p++] = '=';
        for (size_t i = 0; i < pos && i < BUF_SIZE - p - 1; ++i) env_[env_count_][p++] = line[i];
        env_[env_count_][p] = '\0';
        ++env_count_;
    }
}

void Shell::cmd_printf(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: printf <format> [args...]\n");
        return;
    }
    const char* fmt = argv[1];
    int arg_idx = 2;
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            ++fmt;
            if (*fmt == 's') {
                if (arg_idx < argc) { Terminal::write(argv[arg_idx++]); }
                ++fmt;
            } else if (*fmt == 'd' || *fmt == 'i') {
                int val = 0;
                if (arg_idx < argc) {
                    const char* p = argv[arg_idx++];
                    bool neg = (*p == '-');
                    if (neg) ++p;
                    while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
                    if (neg) val = -val;
                }
                char num[16];
                int npos = 0;
                if (val < 0) { Terminal::putchar('-'); val = -val; }
                if (val == 0) { Terminal::putchar('0'); }
                else {
                    while (val > 0) { num[npos++] = '0' + (val % 10); val /= 10; }
                    while (npos > 0) Terminal::putchar(num[--npos]);
                }
                ++fmt;
            } else if (*fmt == 'u') {
                unsigned int val = 0;
                if (arg_idx < argc) {
                    const char* p = argv[arg_idx++];
                    while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');
                }
                char num[16];
                int npos = 0;
                if (val == 0) { Terminal::putchar('0'); }
                else {
                    while (val > 0) { num[npos++] = '0' + (val % 10); val /= 10; }
                    while (npos > 0) Terminal::putchar(num[--npos]);
                }
                ++fmt;
            } else if (*fmt == 'c') {
                if (arg_idx < argc) Terminal::putchar(argv[arg_idx++][0]);
                ++fmt;
            } else if (*fmt == '%') {
                Terminal::putchar('%');
                ++fmt;
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned int val = 0;
                if (arg_idx < argc) {
                    const char* p = argv[arg_idx++];
                    while (*p >= '0' && *p <= '9') val = val * 16 + (*p++ - '0');
                    // Also handle a-f
                }
                char hex[16];
                int hpos = 0;
                if (val == 0) { Terminal::putchar('0'); }
                else {
                    while (val > 0) {
                        unsigned int d = val & 0xF;
                        hex[hpos++] = d < 10 ? '0' + d : (*fmt == 'X' ? 'A' : 'a') + (d - 10);
                        val >>= 4;
                    }
                    while (hpos > 0) Terminal::putchar(hex[--hpos]);
                }
                ++fmt;
            } else {
                Terminal::putchar('%');
                Terminal::putchar(*fmt);
                ++fmt;
            }
        } else if (*fmt == '\\' && *(fmt + 1)) {
            ++fmt;
            switch (*fmt) {
            case 'n': Terminal::putchar('\n'); break;
            case 't': Terminal::putchar('\t'); break;
            case 'r': Terminal::putchar('\r'); break;
            case '\\': Terminal::putchar('\\'); break;
            default: Terminal::putchar('\\'); Terminal::putchar(*fmt); break;
            }
            ++fmt;
        } else {
            Terminal::putchar(*fmt);
            ++fmt;
        }
    }
}

void Shell::cmd_test(int argc, const char** argv) {
    // Parse [ expr ]
    int start = 1;
    bool negate = false;
    if (argc > 1 && str_cmp(argv[1], "!") == 0) { negate = true; start = 2; }

    // Remove trailing ] for [ command
    int end = argc;
    if (argc > 1 && str_cmp(argv[0], "[") == 0 && str_cmp(argv[argc - 1], "]") == 0) {
        end = argc - 1;
    }

    bool result = false;

    if (end <= start) {
        result = false;
    } else if (end - start == 1) {
        // test <string> — true if non-empty
        result = argv[start][0] != '\0';
    } else if (end - start == 3) {
        const char* a = argv[start];
        const char* op = argv[start + 1];
        const char* b = argv[start + 2];
        if (str_cmp(op, "=") == 0 || str_cmp(op, "==") == 0) {
            result = str_cmp(a, b) == 0;
        } else if (str_cmp(op, "!=") == 0) {
            result = str_cmp(a, b) != 0;
        } else if (str_cmp(op, "-eq") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va == vb;
        } else if (str_cmp(op, "-ne") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va != vb;
        } else if (str_cmp(op, "-lt") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va < vb;
        } else if (str_cmp(op, "-le") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va <= vb;
        } else if (str_cmp(op, "-gt") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va > vb;
        } else if (str_cmp(op, "-ge") == 0) {
            int va = 0, vb = 0;
            for (const char* p = a; *p; ++p) va = va * 10 + (*p - '0');
            for (const char* p = b; *p; ++p) vb = vb * 10 + (*p - '0');
            result = va >= vb;
        } else if (str_cmp(op, "-z") == 0) {
            result = a[0] == '\0';
        } else if (str_cmp(op, "-n") == 0) {
            result = a[0] != '\0';
        } else if (op[0] == '-' && op[1] != '\0' && op[2] == '\0') {
            // File tests
            auto* vn = kernel::vfs::resolve(a);
            switch (op[1]) {
            case 'e': result = vn != nullptr; break;
            case 'f': result = vn && (vn->mode & kernel::vfs::S_IFREG); break;
            case 'd': result = vn && (vn->mode & kernel::vfs::S_IFDIR); break;
            case 'r': result = vn != nullptr; break;
            case 'w': result = vn != nullptr; break;
            case 'x': result = vn != nullptr; break;
            case 's': result = vn && vn->size > 0; break;
            default: result = false; break;
            }
        } else {
            result = false;
        }
    } else {
        result = false;
    }

    last_exit_code_ = (result != negate) ? 0 : 1;
}

void Shell::cmd_shift(int argc, const char** argv) {
    int n = 1;
    if (argc > 1) {
        n = 0;
        for (const char* p = argv[1]; *p; ++p) n = n * 10 + (*p - '0');
    }
    if (n <= 0) n = 1;
    if (n > positional_argc_) n = positional_argc_;
    for (int i = 0; i < positional_argc_ - n; ++i) {
        positional_argv_[i] = positional_argv_[i + n];
    }
    positional_argc_ -= n;
}

void Shell::cmd_trap(int argc, const char** argv) {
    if (argc < 2) {
        // List traps
        for (size_t i = 0; i < 32; ++i) {
            if (traps_[i].used) {
                char num[8];
                int pos = 0;
                int n = traps_[i].signo;
                if (n == 0) { Terminal::write("  EXIT"); }
                else {
                    Terminal::write("  ");
                    while (n > 0) { num[pos++] = '0' + (n % 10); n /= 10; }
                    while (pos > 0) Terminal::putchar(num[--pos]);
                }
                Terminal::write(": ");
                Terminal::write(traps_[i].handler);
                Terminal::putchar('\n');
            }
        }
        return;
    }
    if (argc == 2) {
        // Remove trap for signal
        int sig = 0;
        for (const char* p = argv[1]; *p; ++p) sig = sig * 10 + (*p - '0');
        for (size_t i = 0; i < 32; ++i) {
            if (traps_[i].used && traps_[i].signo == sig) {
                traps_[i].used = false;
                return;
            }
        }
        return;
    }
    // trap <action> <signal...>
    int sig = 0;
    for (const char* p = argv[argc - 1]; *p; ++p) sig = sig * 10 + (*p - '0');
    const char* action = argv[1];
    for (size_t i = 0; i < 32; ++i) {
        if (!traps_[i].used) {
            traps_[i].signo = sig;
            size_t pos = 0;
            for (const char* a = action; *a && pos < 255; ++a) traps_[i].handler[pos++] = *a;
            traps_[i].handler[pos] = '\0';
            traps_[i].used = true;
            return;
        }
    }
}

void Shell::cmd_wait(int, const char**) {
    // Wait for all non-current, non-idle tasks to finish
    uint64_t count = kernel::Scheduler::task_count();
    for (uint64_t i = 0; i < count; ++i) {
        auto* task = kernel::Scheduler::task_at(i);
        if (task && task != kernel::Scheduler::current_task() && task->state != kernel::TaskState::TERMINATED) {
            // Spin-wait for simplicity
            while (task->state != kernel::TaskState::TERMINATED) {
                arch::pause();
            }
        }
    }
}

void Shell::cmd_fg(int, const char**) {
    Terminal::write("fg: job control not fully implemented\n");
}

void Shell::cmd_bg(int, const char**) {
    Terminal::write("bg: job control not fully implemented\n");
}

void Shell::cmd_disown(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: disown <task-id>\n");
        return;
    }
    uint64_t id = 0;
    for (const char* p = argv[1]; *p; ++p) id = id * 10 + (*p - '0');
    // Just mark as disowned — we don't track them separately
    Terminal::write("disowned task ");
    char buf[16];
    int pos = 0;
    uint64_t n = id;
    while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; }
    while (pos > 0) Terminal::putchar(buf[--pos]);
    Terminal::putchar('\n');
}

void Shell::cmd_ulimit(int, const char**) {
    Terminal::write("ulimit: not implemented (embedded system)\n");
}

void Shell::cmd_umask(int argc, const char** argv) {
    if (argc < 2) {
        char buf[8];
        int pos = 0;
        int m = umask_;
        for (int i = 0; i < 3; ++i) {
            int digit = (m >> (6 - i * 3)) & 7;
            buf[pos++] = '0' + digit;
        }
        buf[pos] = '\0';
        Terminal::write(buf);
        Terminal::putchar('\n');
        return;
    }
    int new_mask = 0;
    for (const char* p = argv[1]; *p; ++p) {
        if (*p >= '0' && *p <= '7') new_mask = new_mask * 8 + (*p - '0');
    }
    umask_ = new_mask & 0777;
}

void Shell::cmd_times(int, const char**) {
    uint64_t ticks = arch::Timer::ticks();
    uint64_t secs = ticks / 1000;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    secs %= 60;
    mins %= 60;
    Terminal::write("shell running time: ");
    char buf[16];
    int pos = 0;
    uint64_t n = hours;
    while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; }
    while (pos > 0) Terminal::putchar(buf[--pos]);
    Terminal::write("h");
    pos = 0; n = mins;
    while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; }
    while (pos > 0) Terminal::putchar(buf[--pos]);
    Terminal::write("m");
    pos = 0; n = secs;
    while (n > 0) { buf[pos++] = '0' + (n % 10); n /= 10; }
    while (pos > 0) Terminal::putchar(buf[--pos]);
    Terminal::write("s\n");
}

void Shell::cmd_logout(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    const char* exit_argv[] = {"exit"};
    cmd_exit(1, exit_argv);
}

void Shell::cmd_dirs(int, const char**) {
    Terminal::write("Directory stack:\n");
    for (size_t i = 0; i < dir_stack_count_; ++i) {
        Terminal::write("  ");
        Terminal::write(dir_stack_[i]);
        Terminal::putchar('\n');
    }
    if (dir_stack_count_ == 0) Terminal::write("  (empty)\n");
}

void Shell::cmd_pushd(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: pushd <directory>\n");
        return;
    }
    if (dir_stack_count_ >= Shell::MAX_DIR_STACK) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("pushd: directory stack full\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    auto* vn = kernel::vfs::resolve(argv[1]);
    if (!vn || !(vn->mode & kernel::vfs::S_IFDIR)) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("pushd: not a directory: ");
        Terminal::write(argv[1]);
        Terminal::putchar('\n');
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    size_t pos = 0;
    for (const char* p = argv[1]; *p && pos < BUF_SIZE - 1; ++p) dir_stack_[dir_stack_count_][pos++] = *p;
    dir_stack_[dir_stack_count_][pos] = '\0';
    ++dir_stack_count_;
    cmd_cd(2, argv);
}

void Shell::cmd_popd(int, const char**) {
    if (dir_stack_count_ == 0) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("popd: directory stack empty\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    --dir_stack_count_;
    // Change to popped directory
    const char* cd_argv[] = {"cd", dir_stack_[dir_stack_count_]};
    cmd_cd(2, cd_argv);
}

void Shell::cmd_ls(int argc, const char** argv) {
    const char* path = (argc < 2) ? "." : argv[1];
    auto* vn = kernel::vfs::resolve(path);
    if (!vn) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("ls: cannot access '");
        Terminal::write(path);
        Terminal::write("': No such file or directory\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }
    if (!(vn->mode & kernel::vfs::S_IFDIR)) {
        Terminal::write(path);
        Terminal::putchar('\n');
        return;
    }

    uint64_t pos = 0;
    kernel::vfs::Dirent dent;
    while (vn->ops->readdir(*vn, pos, dent) == 0) {
        if (dent.d_name[0] == '\0') continue;
        if (str_cmp(dent.d_name, ".") == 0 || str_cmp(dent.d_name, "..") == 0) continue;

        auto* child = vn->ops->lookup(*vn, dent.d_name);
        if (child) {
            if (child->mode & kernel::vfs::S_IFDIR) {
                Terminal::set_fg(0x00AAFF);
            } else {
                Terminal::set_fg(0xC0C0C0);
            }
        } else {
            Terminal::set_fg(0xC0C0C0);
        }
        Terminal::write(dent.d_name);
        Terminal::set_fg(0xC0C0C0);
        Terminal::putchar(' ');
    }
    Terminal::putchar('\n');
}

static void print_ip(net::Ipv4Addr ip) {
    char buf[16];
    int pos = 0;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) buf[pos++] = '.';
        uint8_t n = ip.addr[i];
        if (n >= 100) buf[pos++] = '0' + (n / 100);
        if (n >= 10) buf[pos++] = '0' + ((n / 10) % 10);
        buf[pos++] = '0' + (n % 10);
    }
    buf[pos] = '\0';
    Terminal::write(buf);
}

static void print_mac(net::MacAddr mac) {
    char buf[18];
    int pos = 0;
    for (int i = 0; i < 6; ++i) {
        if (i > 0) buf[pos++] = ':';
        const char* hex = "0123456789abcdef";
        buf[pos++] = hex[(mac.addr[i] >> 4) & 0xF];
        buf[pos++] = hex[mac.addr[i] & 0xF];
    }
    buf[pos] = '\0';
    Terminal::write(buf);
}

void Shell::cmd_ifconfig(int argc, const char** argv) {
    if (!net::g_nic) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("ifconfig: no network interface\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    auto& nic = *net::g_nic;

    if (argc >= 2) {
        // Parse IP
        uint8_t a[4] = {0, 0, 0, 0};
        int octet = 0;
        int val = 0;
        const char* p = argv[1];
        while (*p && octet < 4) {
            if (*p == '.') {
                a[octet++] = static_cast<uint8_t>(val);
                val = 0;
            } else if (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
            }
            ++p;
        }
        if (octet < 4) a[octet] = static_cast<uint8_t>(val);
        nic.ip = net::Ipv4Addr{{a[0], a[1], a[2], a[3]}};

        if (argc >= 3) {
            octet = 0; val = 0; p = argv[2];
            while (*p && octet < 4) {
                if (*p == '.') { a[octet++] = static_cast<uint8_t>(val); val = 0; }
                else if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0');
                ++p;
            }
            if (octet < 4) a[octet] = static_cast<uint8_t>(val);
            nic.subnet = net::Ipv4Addr{{a[0], a[1], a[2], a[3]}};
        }

        if (argc >= 4) {
            octet = 0; val = 0; p = argv[3];
            while (*p && octet < 4) {
                if (*p == '.') { a[octet++] = static_cast<uint8_t>(val); val = 0; }
                else if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0');
                ++p;
            }
            if (octet < 4) a[octet] = static_cast<uint8_t>(val);
            nic.gateway = net::Ipv4Addr{{a[0], a[1], a[2], a[3]}};
        }

        Terminal::set_fg(0x00FF00);
        Terminal::write("ifconfig: interface configured\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    // Show status
    Terminal::write(nic.name ? nic.name : "eth0");
    Terminal::write(": ");
    print_mac(nic.mac);
    Terminal::write("\n  inet ");
    print_ip(nic.ip);
    Terminal::write("  netmask ");
    print_ip(nic.subnet);
    Terminal::write("  gateway ");
    print_ip(nic.gateway);
    Terminal::write("\n");
}

static int parse_ip(const char* str, net::Ipv4Addr& out) {
    uint8_t a[4] = {0, 0, 0, 0};
    int octet = 0;
    int val = 0;
    while (*str && octet < 4) {
        if (*str == '.') {
            if (val > 255) return -1;
            a[octet++] = static_cast<uint8_t>(val);
            val = 0;
        } else if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
        } else {
            return -1;
        }
        ++str;
    }
    if (octet < 4) {
        if (val > 255) return -1;
        a[octet] = static_cast<uint8_t>(val);
    }
    out = net::Ipv4Addr{{a[0], a[1], a[2], a[3]}};
    return 0;
}

void Shell::cmd_ping(int argc, const char** argv) {
    if (argc < 2) {
        Terminal::write("Usage: ping <ip> [count]\n");
        return;
    }

    net::Ipv4Addr dst;
    if (parse_ip(argv[1], dst) != 0) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("ping: invalid IP address: ");
        Terminal::write(argv[1]);
        Terminal::write("\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    // Loopback works without a NIC
    bool is_loopback = (dst.as_u32() == 0x7F000001);

    if (!net::g_nic && !is_loopback) {
        Terminal::set_fg(0xFF4444);
        Terminal::write("ping: no network interface\n");
        Terminal::set_fg(0xC0C0C0);
        return;
    }

    int count = 4;
    if (argc >= 3) {
        count = 0;
        for (const char* p = argv[2]; *p; ++p) count = count * 10 + (*p - '0');
    }

    Terminal::write("PING ");
    print_ip(dst);
    Terminal::write(": 56 data bytes\n");

    for (int i = 0; i < count; ++i) {
        net::net_icmp_clear_reply();
        uint16_t seq = static_cast<uint16_t>(i);
        uint16_t id = 0x1337;

        uint8_t payload[56];
        uint64_t sent_tick = arch::Timer::ticks();
        for (size_t p = 0; p < sizeof(payload); ++p) payload[p] = static_cast<uint8_t>(p + i);

        bool sent;
        if (is_loopback) {
            // Synthesize reply directly
            net::IcmpEchoReply r;
            r.received = true;
            r.ident = id;
            r.seq = seq;
            r.rx_tick = arch::Timer::ticks();
            r.src = dst;
            *const_cast<net::IcmpEchoReply*>(net::net_icmp_last_reply()) = r;
            sent = true;
        } else {
            sent = net::net_send_icmp_echo(*net::g_nic, dst, id, seq, payload, sizeof(payload));
            if (sent) {
                uint64_t deadline = sent_tick + 1000;
                while (arch::Timer::ticks() < deadline) {
                    for (int p = 0; p < 10; ++p)
                        if (net::net_poll(*net::g_nic)) break;
                    auto* r = net::net_icmp_last_reply();
                    if (r && r->ident == id && r->seq == seq) break;
                    arch::pause();
                }
            }
        }

        auto* reply = net::net_icmp_last_reply();
        if (reply && reply->ident == id && reply->seq == seq) {
            uint64_t rtt = reply->rx_tick - sent_tick;
            Terminal::write("64 bytes from ");
            print_ip(reply->src);
            Terminal::write(": icmp_seq=");
            char nbuf[16];
            int np = 0;
            uint16_t ns = seq;
            while (ns > 0) { nbuf[np++] = '0' + (ns % 10); ns /= 10; }
            while (np > 0) Terminal::putchar(nbuf[--np]);
            Terminal::write(" ttl=64 time=");
            np = 0;
            uint64_t r = rtt;
            while (r > 0) { nbuf[np++] = '0' + (r % 10); r /= 10; }
            while (np > 0) Terminal::putchar(nbuf[--np]);
            Terminal::write(" ms\n");
        } else {
            Terminal::write("Request timeout for icmp_seq=");
            char nbuf[16];
            int np = 0;
            uint16_t ns = static_cast<uint16_t>(i);
            while (ns > 0) { nbuf[np++] = '0' + (ns % 10); ns /= 10; }
            while (np > 0) Terminal::putchar(nbuf[--np]);
            Terminal::write("\n");
        }

        uint64_t wait_end = arch::Timer::ticks() + 100;
        while (arch::Timer::ticks() < wait_end) arch::pause();
    }
}

} // namespace service
