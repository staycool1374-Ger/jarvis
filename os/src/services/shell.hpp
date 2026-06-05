/// @file shell.hpp
/// @brief Interactive command-line shell with built-in and user commands.

#pragma once

#include <types.hpp>

namespace service {

/// @brief Interactive shell with command registration and dispatch.
class Shell {
public:
    /// @brief Initialises the shell and registers built-in commands.
    static void init();
    /// @brief Parses and executes a command string.
    /// @param cmd The command line to execute.
    static void execute(const char* cmd);

    /// @brief Type for shell command handler functions.
    using CommandFunc = void (*)(int argc, const char** argv);
    /// @brief Describes a registered shell command.
    struct Command {
        const char* name;
        const char* help;
        CommandFunc func;
    };

    /// @brief Registers a new shell command.
    /// @param name Command name (used for matching).
    /// @param help Help text shown in "help" command.
    /// @param func Handler function.
    static void register_command(const char* name, const char* help, CommandFunc func);
    /// @brief Main shell loop (reads input, dispatches commands).
    static void shell_task_main();

private:
    static constexpr size_t MAX_COMMANDS = 64;
    static constexpr size_t MAX_ARGS = 16;
    static constexpr size_t BUF_SIZE = 256;

    static Command commands_[MAX_COMMANDS];
    static size_t num_commands_;
    static bool initialized_;
    static char work_dir_[BUF_SIZE];

    /// @brief Parses a command line and dispatches to the matching command.
    /// @param line Raw input line to parse.
    static void parse_and_exec(const char* line);

    /// @brief Built-in: lists available commands.
    static void cmd_help(int argc, const char** argv);
    /// @brief Built-in: clears the terminal.
    static void cmd_clear(int argc, const char** argv);
    /// @brief Built-in: prints its arguments.
    static void cmd_echo(int argc, const char** argv);
    /// @brief Built-in: displays system uptime.
    static void cmd_uptime(int argc, const char** argv);
    /// @brief Built-in: lists running tasks.
    static void cmd_tasks(int argc, const char** argv);
    /// @brief Built-in: shows memory usage info.
    static void cmd_meminfo(int argc, const char** argv);
    /// @brief Built-in: reboots the system.
    static void cmd_reboot(int argc, const char** argv);
    /// @brief Built-in: runs a program by name.
    static void cmd_run(int argc, const char** argv);
    /// @brief Built-in: shows kernel version.
    static void cmd_version(int argc, const char** argv);
    /// @brief Built-in: runs the CPU benchmark.
    static void cmd_bench(int argc, const char** argv);
    /// @brief Built-in: lists background jobs.
    static void cmd_jobs(int argc, const char** argv);
    /// @brief Built-in: loads a kernel driver.
    static void cmd_modprobe(int argc, const char** argv);
    /// @brief Built-in: lists available drivers.
    static void cmd_modlist(int argc, const char** argv);
    /// @brief Built-in: lists installable programs.
    static void cmd_listprog(int argc, const char** argv);
    /// @brief Built-in: runs kernel self-tests.
    static void cmd_selftest(int argc, const char** argv);
    /// @brief Built-in: change working directory.
    static void cmd_cd(int argc, const char** argv);
    /// @brief Built-in: set environment variable.
    static void cmd_export(int argc, const char** argv);
    /// @brief Built-in: run a userspace ELF from initrd as a new task.
    static void cmd_runelf(int argc, const char** argv);
    /// @brief Built-in: shut down the system.
    static void cmd_exit(int argc, const char** argv);
};

} // namespace service
