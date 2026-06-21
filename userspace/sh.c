#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#define MAX_ARGS 16
#define MAX_CMDS 8
#define LINE_BUF 256

static char line[LINE_BUF];
static int last_status = 0;

static void print_prompt(void) {
    printf("sh$ ");
}

static int read_line(void) {
    int i = 0;
    char c;
    while (i < LINE_BUF - 1) {
        if (read(0, &c, 1) != 1) return -1;
        if (c == '\n') break;
        if (c == '\r') break;
        if (c == '\b' || c == 127) {
            if (i > 0) { --i; write(1, "\b \b", 3); }
            continue;
        }
        write(1, &c, 1);
        line[i++] = c;
    }
    line[i] = '\0';
    write(1, "\n", 1);
    return i;
}

struct Command {
    char* argv[MAX_ARGS + 1];
    int argc;
    const char* redirect_in;
    const char* redirect_out;
};

static int parse_line(struct Command cmds[], int* ncmds) {
    int i = 0;
    while (line[i] == ' ') ++i;
    if (line[i] == '\0') return -1;

    char* pipeline[MAX_CMDS];
    int np = 0;
    pipeline[np++] = &line[i];
    for (int j = i; line[j] && np < MAX_CMDS; ++j) {
        if (line[j] == '|') {
            line[j] = '\0';
            pipeline[np++] = &line[j + 1];
        }
    }

    for (int c = 0; c < np; ++c) {
        struct Command* cmd = &cmds[c];
        cmd->argc = 0;
        cmd->redirect_in = 0;
        cmd->redirect_out = 0;

        char* p = pipeline[c];
        while (*p == ' ') ++p;

        while (*p && cmd->argc < MAX_ARGS) {
            if (*p == '<') {
                ++p;
                while (*p == ' ') ++p;
                cmd->redirect_in = p;
                while (*p && *p != ' ') ++p;
                if (*p) { *p++ = '\0'; }
                continue;
            }
            if (*p == '>') {
                ++p;
                while (*p == ' ') ++p;
                cmd->redirect_out = p;
                while (*p && *p != ' ') ++p;
                if (*p) { *p++ = '\0'; }
                continue;
            }
            cmd->argv[cmd->argc++] = p;
            while (*p && *p != ' ') ++p;
            if (*p) { *p++ = '\0'; }
            while (*p == ' ') ++p;
        }
        cmd->argv[cmd->argc] = 0;
    }

    *ncmds = np;
    return np;
}

static int run_command(struct Command* cmd, int input_fd, int output_fd) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) return (int)pid;

    if (input_fd != -1) {
        dup2(input_fd, 0);
        close(input_fd);
    }
    if (output_fd != -1) {
        dup2(output_fd, 1);
        close(output_fd);
    }

    if (cmd->redirect_in) {
        int fd = open(cmd->redirect_in, 0);
        if (fd >= 0) { dup2(fd, 0); close(fd); }
    }
    if (cmd->redirect_out) {
        int fd = open(cmd->redirect_out, 1);
        if (fd >= 0) { dup2(fd, 1); close(fd); }
    }

    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char* target = cmd->argv[1] ? cmd->argv[1] : "/";
        if (chdir(target) < 0)
            printf("cd: %s: no such directory\n", target);
        _exit(0);
    }

    if (strcmp(cmd->argv[0], "export") == 0) {
        if (cmd->argv[1]) printf("export %s\n", cmd->argv[1]);
        _exit(0);
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        _exit(0);
    }

    __syscall5(20, (long)cmd->argv[0], (long)cmd->argv, 0, 0);
    printf("sh: %s: not found\n", cmd->argv[0]);
    _exit(127);
    return -1;
}

static int execute_pipeline(struct Command cmds[], int ncmds) {
    int prev_fd = -1;
    int first_pid = -1;

    for (int i = 0; i < ncmds; ++i) {
        int pipe_fd[2] = {-1, -1};
        int out_fd = -1;

        if (i < ncmds - 1) {
            if (pipe(pipe_fd) < 0) return -1;
            out_fd = pipe_fd[1];
        }

        int pid = run_command(&cmds[i], prev_fd, out_fd);
        if (i == 0) first_pid = pid;

        if (prev_fd != -1) close(prev_fd);
        if (out_fd != -1) close(out_fd);

        prev_fd = pipe_fd[0];
    }

    int status = 0;
    for (int i = 0; i < ncmds; ++i) {
        int wstatus;
        pid_t r = waitpid(-1, &wstatus, 0);
        if (r > 0) status = wstatus;
    }
    return status;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Jarvis RTOS Userspace Shell\n");

    while (1) {
        print_prompt();
        if (read_line() < 0) break;

        struct Command cmds[MAX_CMDS];
        int ncmds;
        if (parse_line(cmds, &ncmds) <= 0) continue;

        if (cmds[0].argc == 0) continue;

        if (strcmp(cmds[0].argv[0], "cd") == 0 && ncmds == 1) {
            const char* target = cmds[0].argv[1] ? cmds[0].argv[1] : "/";
            if (chdir(target) < 0)
                printf("cd: %s: no such directory\n", target);
            continue;
        }

        if (strcmp(cmds[0].argv[0], "exit") == 0) break;

        last_status = execute_pipeline(cmds, ncmds);
    }

    printf("sh: exiting\n");
    return 0;
}