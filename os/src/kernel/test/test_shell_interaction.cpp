#include <test.hpp>
#include <logger.hpp>
#include <services/shell.hpp>
#include <services/terminal/terminal.hpp>
#include <kernel/arch/io.hpp>
#include <constants.hpp>

using namespace kernel;

static void serial_flush_rx() {
    while (arch::inb(arch::COM1_LSR) & 1) {
        arch::inb(arch::COM1);
    }
}

static bool serial_read_all(char* buf, size_t max, int iterations) {
    size_t pos = 0;
    for (int i = 0; i < iterations && pos < max - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf[pos] = '\0';
    return pos > 0;
}

static bool serial_contains(const char* buf, const char* needle) {
    for (size_t i = 0; buf[i] != '\0'; ++i) {
        size_t j;
        for (j = 0; needle[j] != '\0'; ++j) {
            if (buf[i + j] != needle[j]) break;
        }
        if (needle[j] == '\0') return true;
    }
    return false;
}

JARVIS_TEST(shell_loopback_manual_string) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    const char* test_str = "HelloWorld";
    for (const char* p = test_str; *p; ++p) {
        while ((arch::inb(arch::COM1_LSR) & 0x20) == 0);
        arch::outb(arch::COM1, *p);
    }

    char buf[64];
    bool ok = serial_read_all(buf, sizeof(buf), 100000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "HelloWorld"));
}

JARVIS_TEST(shell_terminal_write) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("TESTMARKER");

    char buf[256];
    bool ok = serial_read_all(buf, sizeof(buf), 200000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "TESTMARKER"));
}

JARVIS_TEST(shell_simple_write) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("SIMPLE_TEST");

    char buf[64];
    bool ok = serial_read_all(buf, sizeof(buf), 200000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "SIMPLE_TEST"));
}

JARVIS_TEST(shell_write_with_newline) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("LINE1\nLINE2\n");

    char buf[512];
    bool ok = serial_read_all(buf, sizeof(buf), 500000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "LINE1"));
    JARVIS_ASSERT(serial_contains(buf, "LINE2"));
}

JARVIS_TEST(shell_echo_and_write) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("ALPHA");
    service::Terminal::write("BETA");

    char buf[512];
    bool ok = serial_read_all(buf, sizeof(buf), 500000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "ALPHA"));
    JARVIS_ASSERT(serial_contains(buf, "BETA"));
}

JARVIS_TEST(shell_heap_delete) {
    uint8_t mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    char* tmp = new char[8];
    delete[] tmp;
    service::Terminal::write("END");

    char buf[512];
    bool ok = serial_read_all(buf, sizeof(buf), 500000);
    arch::outb(arch::COM1 + 4, mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf, "END"));
}

JARVIS_TEST(shell_execute_serial) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("garbing");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 100000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, "Unbekannt"));
}

JARVIS_TEST(shell_help_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("help");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, "Verfueg"));
}

JARVIS_TEST(shell_echo_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("echo Hello World");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, "Hello"));
}

JARVIS_TEST(shell_uptime_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("uptime");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, "Uptime"));
}

JARVIS_TEST(shell_pwd_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("pwd");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, "/"));
}

JARVIS_TEST(shell_which_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("locate help");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, " ("));
    JARVIS_ASSERT(serial_contains(buf1, "help"));
}

JARVIS_TEST(shell_help_shows_which) {
    // Verify "locate" is registered and would appear in help output.
    // Can't capture full help via serial loopback (16-byte RX FIFO limit),
    // so instead: run "locate locate" — dispatch proves registration,
    // and cmd_which finding itself proves it's in the command list.
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("locate locate");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, " ("));
    JARVIS_ASSERT(serial_contains(buf1, "locate"));
}

JARVIS_TEST(shell_which_notfound) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("locate xyzzy");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
    JARVIS_ASSERT(serial_contains(buf1, ": not"));
    JARVIS_ASSERT(serial_contains(buf1, "xyzzy"));
}

JARVIS_TEST(shell_env_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("env");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
}

JARVIS_TEST(shell_export_command) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("export FOO=bar");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
}

JARVIS_TEST(shell_sleep_zero) {
    uint8_t orig_mcr = arch::inb(arch::COM1 + 4);
    arch::outb(arch::COM1 + 4, orig_mcr | 0x10);
    serial_flush_rx();

    service::Terminal::write("BEFORE");
    service::Shell::execute("sleep 0");

    char buf1[128];
    size_t pos = 0;
    for (int i = 0; i < 200000 && pos < sizeof(buf1) - 1; ++i) {
        if (arch::inb(arch::COM1_LSR) & 1) {
            buf1[pos++] = arch::inb(arch::COM1);
        }
        arch::pause();
    }
    buf1[pos] = '\0';

    service::Terminal::write("AFTER");

    char buf2[128];
    bool ok = serial_read_all(buf2, sizeof(buf2), 500000);
    arch::outb(arch::COM1 + 4, orig_mcr);

    JARVIS_ASSERT(ok);
    JARVIS_ASSERT(serial_contains(buf1, "BEFORE"));
    JARVIS_ASSERT(serial_contains(buf2, "AFTER"));
}

void register_shell_interaction_tests() {
    Logger::info("Registering shell interaction tests");
    JARVIS_REGISTER_RELEASE_TEST(shell_loopback_manual_string);
    JARVIS_REGISTER_RELEASE_TEST(shell_terminal_write);
    JARVIS_REGISTER_RELEASE_TEST(shell_simple_write);
    JARVIS_REGISTER_RELEASE_TEST(shell_write_with_newline);
    JARVIS_REGISTER_RELEASE_TEST(shell_echo_and_write);
    JARVIS_REGISTER_RELEASE_TEST(shell_heap_delete);
    JARVIS_REGISTER_RELEASE_TEST(shell_execute_serial);
    JARVIS_REGISTER_RELEASE_TEST(shell_help_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_echo_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_uptime_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_pwd_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_help_shows_which);
    JARVIS_REGISTER_RELEASE_TEST(shell_which_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_which_notfound);
    JARVIS_REGISTER_RELEASE_TEST(shell_env_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_export_command);
    JARVIS_REGISTER_RELEASE_TEST(shell_sleep_zero);
}